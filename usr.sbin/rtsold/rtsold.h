/*	$KAME: rtsold.h,v 1.19 2003/04/16 09:48:15 itojun Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
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
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

struct script_msg {
	TAILQ_ENTRY(script_msg)	sm_next;

	char *sm_msg;
};

TAILQ_HEAD(script_msg_head_t, script_msg);

struct ra_opt {
	TAILQ_ENTRY(ra_opt)	rao_next;

	u_int8_t	rao_type;
	struct timespec	rao_expire;
	size_t		rao_len;
	void		*rao_msg;
};

TAILQ_HEAD(rainfo_head, ra_opt);

struct rainfo {
	TAILQ_ENTRY(rainfo)	rai_next;

	struct ifinfo		*rai_ifinfo;
	struct sockaddr_in6	rai_saddr;
	TAILQ_HEAD(, ra_opt)	rai_ra_opt;
};

/* Per-interface tracking info. */
struct ifinfo {
	TAILQ_ENTRY(ifinfo) ifi_next;	/* pointer to the next interface */

	struct sockaddr_dl *sdl; /* link-layer address */
	char ifname[IFNAMSIZ];	/* interface name */
	uint32_t linkid;	/* link ID of this interface */
	int active;		/* interface status */
	int probeinterval;	/* interval of probe timer (if necessary) */
	int probetimer;		/* rest of probe timer */
	int mediareqok;		/* whether the IF supports SIOCGIFMEDIA */
	int otherconfig;	/* need a separate protocol for the "other"
				 * configuration */
	int state;
	int probes;
	int dadcount;
	struct timespec timer;
	struct timespec expire;
#define IFI_DNSOPT_STATE_NOINFO		0
#define IFI_DNSOPT_STATE_RECEIVED     	1
	int ifi_rdnss;		/* RDNSS option state */
	int ifi_dnssl;		/* DNSSL option state */

	int racnt;		/* total # of valid RAs it have got */
	TAILQ_HEAD(, rainfo)	ifi_rainfo;

	size_t rs_datalen;
	u_char *rs_data;
};

/* per interface status */
#define IFS_IDLE	0
#define IFS_DELAY	1
#define IFS_PROBE	2
#define IFS_DOWN	3
#define IFS_TENTATIVE	4

/* Interface list */
extern TAILQ_HEAD(ifinfo_head_t, ifinfo) ifinfo_head;

#define	DNSINFO_ORIGIN_LABEL	"slaac"
/*
 * RFC 3542 API deprecates IPV6_PKTINFO in favor of
 * IPV6_RECVPKTINFO
 */
#ifndef IPV6_RECVPKTINFO
#ifdef IPV6_PKTINFO
#define IPV6_RECVPKTINFO	IPV6_PKTINFO
#endif
#endif
/*
 * RFC 3542 API deprecates IPV6_HOPLIMIT in favor of
 * IPV6_RECVHOPLIMIT
 */
#ifndef IPV6_RECVHOPLIMIT
#ifdef IPV6_HOPLIMIT
#define IPV6_RECVHOPLIMIT	IPV6_HOPLIMIT
#endif
#endif

#ifndef IN6ADDR_LINKLOCAL_ALLROUTERS_INIT
#define IN6ADDR_LINKLOCAL_ALLROUTERS_INIT			\
	{{{ 0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	\
	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 }}}
#endif

#define	TS_CMP(tsp, usp, cmp)						\
	(((tsp)->tv_sec == (usp)->tv_sec) ?				\
	    ((tsp)->tv_nsec cmp (usp)->tv_nsec) :			\
	    ((tsp)->tv_sec cmp (usp)->tv_sec))
#define	TS_ADD(tsp, usp, vsp)						\
	do {								\
		(vsp)->tv_sec = (tsp)->tv_sec + (usp)->tv_sec;		\
		(vsp)->tv_nsec = (tsp)->tv_nsec + (usp)->tv_nsec;	\
		if ((vsp)->tv_nsec >= 1000000000L) {			\
			(vsp)->tv_sec++;				\
			(vsp)->tv_nsec -= 1000000000L;			\
		}							\
	} while (0)
#define	TS_SUB(tsp, usp, vsp)						\
	do {								\
		(vsp)->tv_sec = (tsp)->tv_sec - (usp)->tv_sec;		\
		(vsp)->tv_nsec = (tsp)->tv_nsec - (usp)->tv_nsec;	\
		if ((vsp)->tv_nsec < 0) {				\
			(vsp)->tv_sec--;				\
			(vsp)->tv_nsec += 1000000000L;			\
		}							\
	} while (0)

/* rtsold.c */
struct cap_channel;
extern struct timespec tm_max;
extern int dflag;
extern int aflag;
extern int Fflag;
extern int uflag;
extern const char *otherconf_script;
extern const char *resolvconf_script;
extern struct cap_channel *capllflags, *capscript, *capsendmsg, *capsyslog;

struct ifinfo *find_ifinfo(int);
struct rainfo *find_rainfo(struct ifinfo *, struct sockaddr_in6 *);
void rtsol_timer_update(struct ifinfo *);
extern void warnmsg(int, const char *, const char *, ...)
     __attribute__((__format__(__printf__, 3, 4)));
extern int ra_opt_handler(struct ifinfo *);

/* if.c */
struct nd_opt_hdr;
extern int ifinit(void);
extern int interface_up(char *);
extern int interface_status(struct ifinfo *);
extern int lladdropt_length(struct sockaddr_dl *);
extern void lladdropt_fill(struct sockaddr_dl *, struct nd_opt_hdr *);
extern struct sockaddr_dl *if_nametosdl(char *);

/* rtsol.c */
extern int recvsockopen(void);
extern void rtsol_input(int);

/* cap_llflags.c */
extern int cap_llflags_get(struct cap_channel *, const char *, int *);

/* cap_script.c */
extern int cap_script_run(struct cap_channel *, const char *const *);
extern int cap_script_wait(struct cap_channel *, int *);

/* cap_sendmsg.c */
extern int cap_probe_defrouters(struct cap_channel *, struct ifinfo *);
extern int cap_rssend(struct cap_channel *, struct ifinfo *);

/* dump.c */
extern FILE *rtsold_init_dumpfile(const char *);
extern void rtsold_dump(FILE *);
extern const char *sec2str(const struct timespec *);

/* rtsock.c */
extern int rtsock_open(void);
extern int rtsock_input(int);
