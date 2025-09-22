/*	$OpenBSD: snmpd.h,v 1.1.1.1 2022/09/01 14:20:33 martijn Exp $	*/

/*
 * Copyright (c) 2007, 2008, 2012 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
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
 */

#ifndef SNMPD_H
#define SNMPD_H

#include <sys/tree.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <net/pfvar.h>
#include <net/route.h>

#include <ber.h>
#include <stdio.h>
#include <imsg.h>

#include "log.h"

#ifndef nitems
#define nitems(_a) (sizeof((_a)) / sizeof((_a)[0]))
#endif

/*
 * common definitions for snmpd
 */

#define CONF_FILE		"/etc/snmpd.conf"
#define SNMPD_SOCKET		"/var/run/snmpd.sock"
#define SNMPD_USER		"_snmpd"
#define SNMP_PORT		"161"
#define SNMPTRAP_PORT		"162"

#define SNMPD_MAXSTRLEN		484
#define SNMPD_MAXCOMMUNITYLEN	SNMPD_MAXSTRLEN
#define SNMPD_MAXVARBIND	0x7fffffff
#define SNMPD_MAXVARBINDLEN	1210
#define SNMPD_MAXENGINEIDLEN	32
#define SNMPD_MAXUSERNAMELEN	32
#define SNMPD_MAXCONTEXNAMELEN	32

#define SNMP_USM_MAXDIGESTLEN	48
#define SNMP_USM_SALTLEN	8
#define SNMP_USM_KEYLEN		64
#define SNMP_CIPHER_KEYLEN	16

#define SMALL_READ_BUF_SIZE	1024
#define READ_BUF_SIZE		65535
#define	RT_BUF_SIZE		16384
#define	MAX_RTSOCK_BUF		(2 * 1024 * 1024)

#define SNMP_ENGINEID_OLD	0x00
#define SNMP_ENGINEID_NEW	0x80	/* RFC3411 */

#define SNMP_ENGINEID_FMT_IPv4	1
#define SNMP_ENGINEID_FMT_IPv6	2
#define SNMP_ENGINEID_FMT_MAC	3
#define SNMP_ENGINEID_FMT_TEXT	4
#define SNMP_ENGINEID_FMT_OCT	5
#define SNMP_ENGINEID_FMT_HH	129

#define PEN_OPENBSD		30155

#if DEBUG
#define DPRINTF		log_debug
#else
#define DPRINTF(x...)	do {} while(0)
#endif

/*
 * kroute
 */

struct kroute_node;
struct kroute6_node;
RB_HEAD(kroute_tree, kroute_node);
RB_HEAD(kroute6_tree, kroute6_node);

struct ktable {
	struct kroute_tree	 krt;
	struct kroute6_tree	 krt6;
	u_int			 rtableid;
	u_int			 rdomain;
};

union kaddr {
	struct sockaddr		sa;
	struct sockaddr_in	sin;
	struct sockaddr_in6	sin6;
	struct sockaddr_dl	sdl;
	char			pad[32];
};

struct kroute {
	struct in_addr	prefix;
	struct in_addr	nexthop;
	u_long		ticks;
	u_int16_t	flags;
	u_short		if_index;
	u_int8_t	prefixlen;
	u_int8_t	priority;
};

struct kroute6 {
	struct in6_addr	prefix;
	struct in6_addr	nexthop;
	u_long		ticks;
	u_int16_t	flags;
	u_short		if_index;
	u_int8_t	prefixlen;
	u_int8_t	priority;
};

struct kif_addr {
	u_short			 if_index;
	union kaddr		 addr;
	union kaddr		 mask;
	union kaddr		 dstbrd;

	TAILQ_ENTRY(kif_addr)	 entry;
	RB_ENTRY(kif_addr)	 node;
};

struct kif_arp {
	u_short			 flags;
	u_short			 if_index;
	union kaddr		 addr;
	union kaddr		 target;

	TAILQ_ENTRY(kif_arp)	 entry;
};

struct kif {
	char			 if_name[IF_NAMESIZE];
	char			 if_descr[IFDESCRSIZE];
	u_int8_t		 if_lladdr[ETHER_ADDR_LEN];
	struct if_data		 if_data;
	u_long			 if_ticks;
	int			 if_flags;
	u_short			 if_index;
};
#define	if_mtu		if_data.ifi_mtu
#define	if_type		if_data.ifi_type
#define	if_addrlen	if_data.ifi_addrlen
#define	if_hdrlen	if_data.ifi_hdrlen
#define	if_metric	if_data.ifi_metric
#define	if_link_state	if_data.ifi_link_state
#define	if_baudrate	if_data.ifi_baudrate
#define	if_ipackets	if_data.ifi_ipackets
#define	if_ierrors	if_data.ifi_ierrors
#define	if_opackets	if_data.ifi_opackets
#define	if_oerrors	if_data.ifi_oerrors
#define	if_collisions	if_data.ifi_collisions
#define	if_ibytes	if_data.ifi_ibytes
#define	if_obytes	if_data.ifi_obytes
#define	if_imcasts	if_data.ifi_imcasts
#define	if_omcasts	if_data.ifi_omcasts
#define	if_iqdrops	if_data.ifi_iqdrops
#define	if_oqdrops	if_data.ifi_oqdrops
#define	if_noproto	if_data.ifi_noproto
#define	if_lastchange	if_data.ifi_lastchange
#define	if_capabilities	if_data.ifi_capabilities

#define F_CONNECTED		0x0001
#define F_STATIC		0x0002
#define F_BLACKHOLE		0x0004
#define F_REJECT		0x0008
#define F_DYNAMIC		0x0010

/*
 * pf
 */

enum {	PFRB_TABLES = 1, PFRB_TSTATS, PFRB_ADDRS, PFRB_ASTATS,
	PFRB_IFACES, PFRB_TRANS, PFRB_MAX };

enum {  IN, OUT };
enum {  IPV4, IPV6 };
enum {  PASS, BLOCK };

enum {  PFI_IFTYPE_GROUP, PFI_IFTYPE_INSTANCE };

struct pfr_buffer {
	int	 pfrb_type;	/* type of content, see enum above */
	int	 pfrb_size;	/* number of objects in buffer */
	int	 pfrb_msize;	/* maximum number of objects in buffer */
	void	*pfrb_caddr;	/* malloc'ated memory area */
};

#define PFRB_FOREACH(var, buf)				\
	for ((var) = pfr_buf_next((buf), NULL);		\
	    (var) != NULL;				\
	    (var) = pfr_buf_next((buf), (var)))

/*
 * daemon structures
 */

struct snmpd {
	int			 sc_ncpu;
	int64_t			*sc_cpustates;
	int			 sc_rtfilter;
};

extern struct snmpd *snmpd_env;

/* mib.c */
u_long   smi_getticks(void);

/* kroute.c */
void		 kr_init(void);
void		 kr_shutdown(void);

u_int		 kr_ifnumber(void);
u_long		 kr_iflastchange(void);
int		 kr_updateif(u_int);
u_long		 kr_routenumber(void);

struct kif	*kr_getif(u_short);
struct kif	*kr_getnextif(u_short);
struct kif_addr *kr_getaddr(struct sockaddr *);
struct kif_addr *kr_getnextaddr(struct sockaddr *);

struct kroute	*kroute_first(void);
struct kroute	*kroute_getaddr(in_addr_t, u_int8_t, u_int8_t, int);

struct kif_arp	*karp_first(u_short);
struct kif_arp	*karp_getaddr(struct sockaddr *, u_short, int);

/* pf.c */
void			 pf_init(void);
int			 pf_get_stats(struct pf_status *);
int			 pfr_get_astats(struct pfr_table *, struct pfr_astats *,
			    int *, int);
int			 pfr_get_tstats(struct pfr_table *, struct pfr_tstats *,
			    int *, int);
int			 pfr_buf_grow(struct pfr_buffer *, int);
const void		*pfr_buf_next(struct pfr_buffer *, const void *);
int			 pfi_get_ifaces(const char *, struct pfi_kif *, int *);
int			 pfi_get(struct pfr_buffer *, const char *);
int			 pfi_count(void);
int			 pfi_get_if(struct pfi_kif *, int);
int			 pft_get(struct pfr_buffer *, struct pfr_table *);
int			 pft_count(void);
int			 pft_get_table(struct pfr_tstats *, int);
int			 pfta_get(struct pfr_buffer *, struct pfr_table *);
int			 pfta_get_addr(struct pfr_astats *, int);
int			 pfta_get_nextaddr(struct pfr_astats *, int *);
int			 pfta_get_first(struct pfr_astats *);

/* timer.c */
void		 timer_init(void);

/* util.c */
ssize_t	 sendtofrom(int, void *, size_t, int, struct sockaddr *,
	    socklen_t, struct sockaddr *, socklen_t);
ssize_t	 recvfromto(int, void *, size_t, int, struct sockaddr *,
	    socklen_t *, struct sockaddr *, socklen_t *);
const char *log_in6addr(const struct in6_addr *);
const char *print_host(struct sockaddr_storage *, char *, size_t);
char	*tohexstr(u_int8_t *, int);
uint8_t *fromhexstr(uint8_t *, const char *, size_t);

#endif /* SNMPD_H */
