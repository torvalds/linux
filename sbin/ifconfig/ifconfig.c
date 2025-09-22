/*	$OpenBSD: ifconfig.c,v 1.476 2025/03/22 07:24:49 kevlo Exp $	*/
/*	$NetBSD: ifconfig.c,v 1.40 1997/10/01 02:19:43 enami Exp $	*/

/*
 * Copyright (c) 1983, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*-
 * Copyright (c) 1997, 1998, 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet6/in6_var.h>
#include <netinet6/nd6.h>
#include <arpa/inet.h>
#include <netinet/ip_ipsp.h>
#include <netinet/if_ether.h>
#include <net80211/ieee80211.h>
#include <net80211/ieee80211_ioctl.h>
#include <net/pfvar.h>
#include <net/if_pfsync.h>
#include <net/if_pflow.h>
#include <net/if_pppoe.h>
#include <net/if_trunk.h>
#include <net/if_wg.h>
#include <net/trunklacp.h>
#include <net/if_sppp.h>
#include <net/ppp_defs.h>

#include <netinet/ip_carp.h>

#include <netdb.h>

#include <net/if_vlan_var.h>

#include <netmpls/mpls.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <resolv.h>
#include <util.h>
#include <ifaddrs.h>

#ifndef SMALL
#include <dev/usb/mbim.h>
#include <dev/usb/if_umb.h>
#endif /* SMALL */

#include "ifconfig.h"

#ifndef nitems
#define nitems(_a)	(sizeof((_a)) / sizeof((_a)[0]))
#endif

#define MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))
#define MAXIMUM(a, b)	(((a) > (b)) ? (a) : (b))

#define HWFEATURESBITS							\
	"\024\1CSUM_IPv4\2CSUM_TCPv4\3CSUM_UDPv4"			\
	"\5VLAN_MTU\6VLAN_HWTAGGING\7VLAN_HWOFFLOAD\10CSUM_TCPv6"	\
	"\11CSUM_UDPv6\15TSOv4\16TSOv6\17LRO\20WOL"

struct ifencap {
	unsigned int	 ife_flags;
#define IFE_VNETID_MASK		0xf
#define IFE_VNETID_NOPE		0x0
#define IFE_VNETID_NONE		0x1
#define IFE_VNETID_ANY		0x2
#define IFE_VNETID_SET		0x3
	int64_t		 ife_vnetid;
#define IFE_VNETFLOWID		0x10

#define IFE_PARENT_MASK		0xf00
#define IFE_PARENT_NOPE		0x000
#define IFE_PARENT_NONE		0x100
#define IFE_PARENT_SET		0x200
	char		ife_parent[IFNAMSIZ];

#define IFE_TXHPRIO_SET		0x1000
	int		ife_txhprio;
#define IFE_RXHPRIO_SET		0x2000
	int		ife_rxhprio;
};

struct	ifreq		ifr, ridreq;
struct	in_aliasreq	in_addreq;
struct	in6_ifreq	ifr6;
struct	in6_ifreq	in6_ridreq;
struct	in6_aliasreq	in6_addreq;
struct	sockaddr_in	netmask;

#ifndef SMALL
int	rdomainid;
#endif /* SMALL */

char	ifname[IFNAMSIZ];
int	flags, xflags, setaddr, setipdst, doalias;
u_long	metric, mtu;
int	llprio;
int	clearaddr, sock;
int	newaddr = 0;
int	af = AF_INET;
int	explicit_prefix = 0;
int	Lflag = 1;
int	show_join = 0;

int	showmediaflag;
int	showcapsflag;
int	shownet80211chans;
int	shownet80211nodes;
int	showclasses;
int	showtransceiver;

struct	ifencap;

struct ieee80211_join join;

const	char *lacpmodeactive = "active";
const	char *lacpmodepassive = "passive";
const	char *lacptimeoutfast = "fast";
const	char *lacptimeoutslow = "slow";

void	notealias(const char *, int);
void	setifaddr(const char *, int);
void	setiflladdr(const char *, int);
void	setifdstaddr(const char *, int);
void	setifflags(const char *, int);
void	setifxflags(const char *, int);
void	addaf(const char *, int);
void	removeaf(const char *, int);
void	setifbroadaddr(const char *, int);
void	setifmtu(const char *, int);
void	setifllprio(const char *, int);
void	setifnwid(const char *, int);
void	setifjoin(const char *, int);
void	delifjoin(const char *, int);
void	delifjoinlist(const char *, int);
void	showjoin(const char *, int);
void	setifbssid(const char *, int);
void	setifnwkey(const char *, int);
void	setifwpa(const char *, int);
void	setifwpaprotos(const char *, int);
void	setifwpaakms(const char *, int);
void	setifwpaciphers(const char *, int);
void	setifwpagroupcipher(const char *, int);
void	setifwpakey(const char *, int);
void	setifchan(const char *, int);
void	setifscan(const char *, int);
void	setifnwflag(const char *, int);
void	unsetifnwflag(const char *, int);
void	setifnetmask(const char *, int);
void	setifprefixlen(const char *, int);
void	setvnetid(const char *, int);
void	delvnetid(const char *, int);
void	getvnetid(struct ifencap *);
void	setifparent(const char *, int);
void	delifparent(const char *, int);
void	getifparent(struct ifencap *);
void	getencap(void);
void	setia6flags(const char *, int);
void	setia6pltime(const char *, int);
void	setia6vltime(const char *, int);
void	setia6lifetime(const char *, const char *);
void	setia6eui64(const char *, int);
void	setmedia(const char *, int);
void	setmediaopt(const char *, int);
void	setmediamode(const char *, int);
void	unsetmediamode(const char *, int);
void	clone_create(const char *, int);
void	clone_destroy(const char *, int);
void	unsetmediaopt(const char *, int);
void	setmediainst(const char *, int);
int	prefix(void *val, int);
void	getifgroups(void);
void	setifgroup(const char *, int);
void	unsetifgroup(const char *, int);
void	setgroupattribs(char *, int, char *[]);
int	printgroup(char *, int);
void	setautoconf(const char *, int);
void	settemporary(const char *, int);
void	settrunkport(const char *, int);
void	unsettrunkport(const char *, int);
void	settrunkproto(const char *, int);
void	settrunklacpmode(const char *, int);
void	settrunklacptimeout(const char *, int);
void	trunk_status(void);
void	list_cloners(void);

#ifndef SMALL
void	setifrtlabel(const char *, int);
void	setrdomain(const char *, int);
void	unsetrdomain(const char *, int);
void	setkeepalive(const char *, const char *);
void	unsetkeepalive(const char *, int);
void	carp_status(void);
void	setcarp_advbase(const char *,int);
void	setcarp_advskew(const char *, int);
void	setcarppeer(const char *, int);
void	unsetcarppeer(const char *, int);
void	setcarp_passwd(const char *, int);
void	setcarp_vhid(const char *, int);
void	setcarp_state(const char *, int);
void	setcarpdev(const char *, int);
void	setcarp_nodes(const char *, int);
void	setcarp_balancing(const char *, int);
void	setpfsync_syncdev(const char *, int);
void	setpfsync_maxupd(const char *, int);
void	unsetpfsync_syncdev(const char *, int);
void	setpfsync_syncpeer(const char *, int);
void	unsetpfsync_syncpeer(const char *, int);
void	setpfsync_defer(const char *, int);
void	pfsync_status(void);
void	setvnetflowid(const char *, int);
void	delvnetflowid(const char *, int);
void	getvnetflowid(struct ifencap *);
void	gettxprio(struct ifencap *);
void	settxprio(const char *, int);
void	getrxprio(struct ifencap *);
void	setrxprio(const char *, int);
void	setmplslabel(const char *, int);
void	unsetmplslabel(const char *, int);
void	setpwe3cw(const char *, int);
void	unsetpwe3cw(const char *, int);
void	setpwe3fat(const char *, int);
void	unsetpwe3fat(const char *, int);
void	setpwe3neighbor(const char *, const char *);
void	unsetpwe3neighbor(const char *, int);
void	mpls_status(void);
void	settunnel(const char *, const char *);
void	settunneladdr(const char *, int);
void	deletetunnel(const char *, int);
void	settunnelinst(const char *, int);
void	unsettunnelinst(const char *, int);
void	settunnelttl(const char *, int);
void	settunneldf(const char *, int);
void	settunnelnodf(const char *, int);
void	settunnelecn(const char *, int);
void	settunnelnoecn(const char *, int);
void	setpppoe_dev(const char *,int);
void	setpppoe_svc(const char *,int);
void	setpppoe_ac(const char *,int);
void	pppoe_status(void);
void	setspppproto(const char *, int);
void	setspppname(const char *, int);
void	setspppkey(const char *, int);
void	setsppppeerproto(const char *, int);
void	setsppppeername(const char *, int);
void	setsppppeerkey(const char *, int);
void	setsppppeerflag(const char *, int);
void	unsetsppppeerflag(const char *, int);
void	sppp_status(void);
void	sppp_printproto(const char *, struct sauthreq *);
void	setifpriority(const char *, int);
void	setifpowersave(const char *, int);
void	setifmetric(const char *, int);
void	pflow_status(void);
void	pflow_addr(const char*, struct sockaddr_storage *);
void	setpflow_sender(const char *, int);
void	unsetpflow_sender(const char *, int);
void	setpflow_receiver(const char *, int);
void	unsetpflow_receiver(const char *, int);
void	setpflowproto(const char *, int);
void	setifipdst(const char *, int);
void	setifdesc(const char *, int);
void	unsetifdesc(const char *, int);
void	printifhwfeatures(const char *, int);
void	setpair(const char *, int);
void	unsetpair(const char *, int);
void	umb_status(void);
void	umb_printclasses(char *, int);
int	umb_parse_classes(const char *);
void	umb_setpin(const char *, int);
void	umb_chgpin(const char *, const char *);
void	umb_puk(const char *, const char *);
void	umb_pinop(int, int, const char *, const char *);
void	umb_apn(const char *, int);
void	umb_setclass(const char *, int);
void	umb_roaming(const char *, int);
void	utf16_to_char(uint16_t *, int, char *, size_t);
int	char_to_utf16(const char *, uint16_t *, size_t);
void	transceiver(const char *, int);
void	transceiverdump(const char *, int);

/* WG */
void	setwgpeer(const char *, int);
void	setwgpeerdesc(const char *, int);
void	setwgpeerep(const char *, const char *);
void	setwgpeeraip(const char *, int);
void	setwgpeerpsk(const char *, int);
void	setwgpeerpka(const char *, int);
void	setwgport(const char *, int);
void	setwgkey(const char *, int);
void	setwgrtable(const char *, int);

void	unsetwgpeer(const char *, int);
void	unsetwgpeerdesc(const char *, int);
void	unsetwgpeerpsk(const char *, int);
void	unsetwgpeerall(const char *, int);

void	wg_status(int);
#else
void	setignore(const char *, int);
#endif

struct if_clonereq *get_cloners(void);
int	findmac(const char *);

/*
 * Media stuff.  Whenever a media command is first performed, the
 * currently select media is grabbed for this interface.  If `media'
 * is given, the current media word is modified.  `mediaopt' commands
 * only modify the set and clear words.  They then operate on the
 * current media word later.
 */
uint64_t	media_current;
uint64_t	mediaopt_set;
uint64_t	mediaopt_clear;

int	actions;			/* Actions performed */

#define	A_MEDIA		0x0001		/* media command */
#define	A_MEDIAOPTSET	0x0002		/* mediaopt command */
#define	A_MEDIAOPTCLR	0x0004		/* -mediaopt command */
#define	A_MEDIAOPT	(A_MEDIAOPTSET|A_MEDIAOPTCLR)
#define	A_MEDIAINST	0x0008		/* instance or inst command */
#define	A_MEDIAMODE	0x0010		/* mode command */
#define	A_JOIN		0x0020		/* join */
#define	A_WIREGUARD	0x0040		/* any WireGuard command */
#define A_SILENT	0x8000000	/* doing operation, do not print */

#define	NEXTARG0	0xffffff
#define NEXTARG		0xfffffe
#define	NEXTARG2	0xfffffd

const struct	cmd {
	char	*c_name;
	int	c_parameter;		/* NEXTARG means next argv */
	int	c_action;		/* defered action */
	void	(*c_func)(const char *, int);
	void	(*c_func2)(const char *, const char *);
} cmds[] = {
	{ "up",		IFF_UP,		0,		setifflags } ,
	{ "down",	-IFF_UP,	0,		setifflags },
	{ "arp",	-IFF_NOARP,	0,		setifflags },
	{ "-arp",	IFF_NOARP,	0,		setifflags },
	{ "debug",	IFF_DEBUG,	0,		setifflags },
	{ "-debug",	-IFF_DEBUG,	0,		setifflags },
	{ "alias",	IFF_UP,		0,		notealias },
	{ "-alias",	-IFF_UP,	0,		notealias },
	{ "delete",	-IFF_UP,	0,		notealias },
	{ "netmask",	NEXTARG,	0,		setifnetmask },
	{ "mtu",	NEXTARG,	0,		setifmtu },
	{ "nwid",	NEXTARG,	0,		setifnwid },
	{ "-nwid",	-1,		0,		setifnwid },
	{ "join",	NEXTARG,	0,		setifjoin },
	{ "-join",	NEXTARG,	0,		delifjoin },
	{ "joinlist",	NEXTARG0,	0,		showjoin },
	{ "-joinlist",	-1,		0,		delifjoinlist },
	{ "bssid",	NEXTARG,	0,		setifbssid },
	{ "-bssid",	-1,		0,		setifbssid },
	{ "nwkey",	NEXTARG,	0,		setifnwkey },
	{ "-nwkey",	-1,		0,		setifnwkey },
	{ "wpa",	1,		0,		setifwpa },
	{ "-wpa",	0,		0,		setifwpa },
	{ "wpaakms",	NEXTARG,	0,		setifwpaakms },
	{ "wpaciphers",	NEXTARG,	0,		setifwpaciphers },
	{ "wpagroupcipher", NEXTARG,	0,		setifwpagroupcipher },
	{ "wpaprotos",	NEXTARG,	0,		setifwpaprotos },
	{ "wpakey",	NEXTARG,	0,		setifwpakey },
	{ "-wpakey",	-1,		0,		setifwpakey },
	{ "chan",	NEXTARG0,	0,		setifchan },
	{ "-chan",	-1,		0,		setifchan },
	{ "scan",	NEXTARG0,	0,		setifscan },
	{ "broadcast",	NEXTARG,	0,		setifbroadaddr },
	{ "prefixlen",  NEXTARG,	0,		setifprefixlen},
	{ "vnetid",	NEXTARG,	0,		setvnetid },
	{ "-vnetid",	0,		0,		delvnetid },
	{ "parent",	NEXTARG,	0,		setifparent },
	{ "-parent",	1,		0,		delifparent },
	{ "vlan",	NEXTARG,	0,		setvnetid },
	{ "-vlan",	0,		0,		delvnetid },
	{ "vlandev",	NEXTARG,	0,		setifparent },
	{ "-vlandev",	1,		0,		delifparent },
	{ "group",	NEXTARG,	0,		setifgroup },
	{ "-group",	NEXTARG,	0,		unsetifgroup },
	{ "autoconf",	1,		0,		setautoconf },
	{ "-autoconf",	-1,		0,		setautoconf },
	{ "trunkport",	NEXTARG,	0,		settrunkport },
	{ "-trunkport",	NEXTARG,	0,		unsettrunkport },
	{ "trunkproto",	NEXTARG,	0,		settrunkproto },
	{ "lacpmode",	NEXTARG,	0,		settrunklacpmode },
	{ "lacptimeout", NEXTARG,	0,		settrunklacptimeout },
	{ "anycast",	IN6_IFF_ANYCAST,	0,	setia6flags },
	{ "-anycast",	-IN6_IFF_ANYCAST,	0,	setia6flags },
	{ "tentative",	IN6_IFF_TENTATIVE,	0,	setia6flags },
	{ "-tentative",	-IN6_IFF_TENTATIVE,	0,	setia6flags },
	{ "pltime",	NEXTARG,	0,		setia6pltime },
	{ "vltime",	NEXTARG,	0,		setia6vltime },
	{ "eui64",	0,		0,		setia6eui64 },
	{ "temporary",	1,		0,		settemporary },
	{ "-temporary",	-1,		0,		settemporary },
	{ "soii",	-IFXF_INET6_NOSOII,	0,	setifxflags },
	{ "-soii",	IFXF_INET6_NOSOII,	0,	setifxflags },
	{ "monitor",	IFXF_MONITOR,	0,		setifxflags },
	{ "-monitor",	-IFXF_MONITOR,	0,		setifxflags },
	{ "tcplro",	IFXF_LRO,	0,		setifxflags },
	{ "-tcplro",	-IFXF_LRO,	0,		setifxflags },
#ifndef SMALL
	{ "hwfeatures", NEXTARG0,	0,		printifhwfeatures },
	{ "metric",	NEXTARG,	0,		setifmetric },
	{ "powersave",	NEXTARG0,	0,		setifpowersave },
	{ "-powersave",	-1,		0,		setifpowersave },
	{ "priority",	NEXTARG,	0,		setifpriority },
	{ "rtlabel",	NEXTARG,	0,		setifrtlabel },
	{ "-rtlabel",	-1,		0,		setifrtlabel },
	{ "rdomain",	NEXTARG,	0,		setrdomain },
	{ "-rdomain",	0,		0,		unsetrdomain },
	{ "staticarp",	IFF_STATICARP,	0,		setifflags },
	{ "-staticarp",	-IFF_STATICARP,	0,		setifflags },
	{ "mpls",	IFXF_MPLS,	0,		setifxflags },
	{ "-mpls",	-IFXF_MPLS,	0,		setifxflags },
	{ "mplslabel",	NEXTARG,	0,		setmplslabel },
	{ "-mplslabel",	0,		0,		unsetmplslabel },
	{ "pwecw",	0,		0,		setpwe3cw },
	{ "-pwecw",	0,		0,		unsetpwe3cw },
	{ "pwefat",	0,		0,		setpwe3fat },
	{ "-pwefat",	0,		0,		unsetpwe3fat },
	{ "pweneighbor", NEXTARG2,	0,		NULL, setpwe3neighbor },
	{ "-pweneighbor", 0,		0,		unsetpwe3neighbor },
	{ "advbase",	NEXTARG,	0,		setcarp_advbase },
	{ "advskew",	NEXTARG,	0,		setcarp_advskew },
	{ "carppeer",	NEXTARG,	0,		setcarppeer },
	{ "-carppeer",	1,		0,		unsetcarppeer },
	{ "pass",	NEXTARG,	0,		setcarp_passwd },
	{ "vhid",	NEXTARG,	0,		setcarp_vhid },
	{ "state",	NEXTARG,	0,		setcarp_state },
	{ "carpdev",	NEXTARG,	0,		setcarpdev },
	{ "carpnodes",	NEXTARG,	0,		setcarp_nodes },
	{ "balancing",	NEXTARG,	0,		setcarp_balancing },
	{ "syncdev",	NEXTARG,	0,		setpfsync_syncdev },
	{ "-syncdev",	1,		0,		unsetpfsync_syncdev },
	{ "syncif",	NEXTARG,	0,		setpfsync_syncdev },
	{ "-syncif",	1,		0,		unsetpfsync_syncdev },
	{ "syncpeer",	NEXTARG,	0,		setpfsync_syncpeer },
	{ "-syncpeer",	1,		0,		unsetpfsync_syncpeer },
	{ "maxupd",	NEXTARG,	0,		setpfsync_maxupd },
	{ "defer",	1,		0,		setpfsync_defer },
	{ "-defer",	0,		0,		setpfsync_defer },
	{ "tunnel",	NEXTARG2,	0,		NULL, settunnel },
	{ "tunneladdr",	NEXTARG,	0,		settunneladdr },
	{ "-tunnel",	0,		0,		deletetunnel },
	{ "tunneldomain", NEXTARG,	0,		settunnelinst },
	{ "-tunneldomain", 0,		0,		unsettunnelinst },
	{ "tunnelttl",	NEXTARG,	0,		settunnelttl },
	{ "tunneldf",	0,		0,		settunneldf },
	{ "-tunneldf",	0,		0,		settunnelnodf },
	{ "tunnelecn",	0,		0,		settunnelecn },
	{ "-tunnelecn",	0,		0,		settunnelnoecn },
	{ "vnetflowid",	0,		0,		setvnetflowid },
	{ "-vnetflowid", 0,		0,		delvnetflowid },
	{ "txprio",	NEXTARG,	0,		settxprio },
	{ "rxprio",	NEXTARG,	0,		setrxprio },
	{ "pppoedev",	NEXTARG,	0,		setpppoe_dev },
	{ "pppoesvc",	NEXTARG,	0,		setpppoe_svc },
	{ "-pppoesvc",	1,		0,		setpppoe_svc },
	{ "pppoeac",	NEXTARG,	0,		setpppoe_ac },
	{ "-pppoeac",	1,		0,		setpppoe_ac },
	{ "authproto",	NEXTARG,	0,		setspppproto },
	{ "authname",	NEXTARG,	0,		setspppname },
	{ "authkey",	NEXTARG,	0,		setspppkey },
	{ "peerproto",	NEXTARG,	0,		setsppppeerproto },
	{ "peername",	NEXTARG,	0,		setsppppeername },
	{ "peerkey",	NEXTARG,	0,		setsppppeerkey },
	{ "peerflag",	NEXTARG,	0,		setsppppeerflag },
	{ "-peerflag",	NEXTARG,	0,		unsetsppppeerflag },
	{ "nwflag",	NEXTARG,	0,		setifnwflag },
	{ "-nwflag",	NEXTARG,	0,		unsetifnwflag },
	{ "flowsrc",	NEXTARG,	0,		setpflow_sender },
	{ "-flowsrc",	1,		0,		unsetpflow_sender },
	{ "flowdst",	NEXTARG,	0,		setpflow_receiver },
	{ "-flowdst", 1,		0,		unsetpflow_receiver },
	{ "pflowproto", NEXTARG,	0,		setpflowproto },
	{ "-inet",	AF_INET,	0,		removeaf },
	{ "-inet6",	AF_INET6,	0,		removeaf },
	{ "keepalive",	NEXTARG2,	0,		NULL, setkeepalive },
	{ "-keepalive",	1,		0,		unsetkeepalive },
	{ "add",	NEXTARG,	0,		bridge_add },
	{ "del",	NEXTARG,	0,		bridge_delete },
	{ "addspan",	NEXTARG,	0,		bridge_addspan },
	{ "delspan",	NEXTARG,	0,		bridge_delspan },
	{ "discover",	NEXTARG,	0,		setdiscover },
	{ "-discover",	NEXTARG,	0,		unsetdiscover },
	{ "blocknonip", NEXTARG,	0,		setblocknonip },
	{ "-blocknonip",NEXTARG,	0,		unsetblocknonip },
	{ "learn",	NEXTARG,	0,		setlearn },
	{ "-learn",	NEXTARG,	0,		unsetlearn },
	{ "stp",	NEXTARG,	0,		setstp },
	{ "-stp",	NEXTARG,	0,		unsetstp },
	{ "edge",	NEXTARG,	0,		setedge },
	{ "-edge",	NEXTARG,	0,		unsetedge },
	{ "autoedge",	NEXTARG,	0,		setautoedge },
	{ "-autoedge",	NEXTARG,	0,		unsetautoedge },
	{ "protected",	NEXTARG2,	0,		NULL, bridge_protect },
	{ "-protected",	NEXTARG,	0,		bridge_unprotect },
	{ "ptp",	NEXTARG,	0,		setptp },
	{ "-ptp",	NEXTARG,	0,		unsetptp },
	{ "autoptp",	NEXTARG,	0,		setautoptp },
	{ "-autoptp",	NEXTARG,	0,		unsetautoptp },
	{ "flush",	0,		0,		bridge_flush },
	{ "flushall",	0,		0,		bridge_flushall },
	{ "static",	NEXTARG2,	0,		NULL, bridge_addaddr },
	{ "endpoint",	NEXTARG2,	0,		NULL, bridge_addendpoint },
	{ "-endpoint",	NEXTARG,	0,		bridge_delendpoint },
	{ "deladdr",	NEXTARG,	0,		bridge_deladdr },
	{ "maxaddr",	NEXTARG,	0,		bridge_maxaddr },
	{ "addr",	0,		0,		bridge_addrs },
	{ "hellotime",	NEXTARG,	0,		bridge_hellotime },
	{ "fwddelay",	NEXTARG,	0,		bridge_fwddelay },
	{ "maxage",	NEXTARG,	0,		bridge_maxage },
	{ "proto",	NEXTARG,	0,		bridge_proto },
	{ "ifpriority",	NEXTARG2,	0,		NULL, bridge_ifprio },
	{ "ifcost",	NEXTARG2,	0,		NULL, bridge_ifcost },
	{ "-ifcost",	NEXTARG,	0,		bridge_noifcost },
	{ "timeout",	NEXTARG,	0,		bridge_timeout },
	{ "holdcnt",	NEXTARG,	0,		bridge_holdcnt },
	{ "spanpriority", NEXTARG,	0,		bridge_priority },
	{ "ipdst",	NEXTARG,	0,		setifipdst },
#if 0
	/* XXX `rule` special-cased below */
	{ "rule",	0,		0,		bridge_rule },
#endif
	{ "rules",	NEXTARG,	0,		bridge_rules },
	{ "rulefile",	NEXTARG,	0,		bridge_rulefile },
	{ "flushrule",	NEXTARG,	0,		bridge_flushrule },
	{ "description", NEXTARG,	0,		setifdesc },
	{ "descr",	NEXTARG,	0,		setifdesc },
	{ "-description", 1,		0,		unsetifdesc },
	{ "-descr",	1,		0,		unsetifdesc },
	{ "wol",	IFXF_WOL,	0,		setifxflags },
	{ "-wol",	-IFXF_WOL,	0,		setifxflags },
	{ "pin",	NEXTARG,	0,		umb_setpin },
	{ "chgpin",	NEXTARG2,	0,		NULL, umb_chgpin },
	{ "puk",	NEXTARG2,	0,		NULL, umb_puk },
	{ "apn",	NEXTARG,	0,		umb_apn },
	{ "-apn",	-1,		0,		umb_apn },
	{ "class",	NEXTARG0,	0,		umb_setclass },
	{ "-class",	-1,		0,		umb_setclass },
	{ "roaming",	1,		0,		umb_roaming },
	{ "-roaming",	0,		0,		umb_roaming },
	{ "patch",	NEXTARG,	0,		setpair },
	{ "-patch",	1,		0,		unsetpair },
	{ "addlocal",	NEXTARG,	0,		addlocal },
	{ "transceiver", NEXTARG0,	0,		transceiver },
	{ "sff",	NEXTARG0,	0,		transceiver },
	{ "sffdump",	0,		0,		transceiverdump },

	{ "wgpeer",	NEXTARG,	A_WIREGUARD,	setwgpeer},
	{ "wgdescription", NEXTARG,	A_WIREGUARD,	setwgpeerdesc},
	{ "wgdescr",	NEXTARG,	A_WIREGUARD,	setwgpeerdesc},
	{ "wgendpoint",	NEXTARG2,	A_WIREGUARD,	NULL, setwgpeerep},
	{ "wgaip",	NEXTARG,	A_WIREGUARD,	setwgpeeraip},
	{ "wgpsk",	NEXTARG,	A_WIREGUARD,	setwgpeerpsk},
	{ "wgpka",	NEXTARG,	A_WIREGUARD,	setwgpeerpka},
	{ "wgport",	NEXTARG,	A_WIREGUARD,	setwgport},
	{ "wgkey",	NEXTARG,	A_WIREGUARD,	setwgkey},
	{ "wgrtable",	NEXTARG,	A_WIREGUARD,	setwgrtable},
	{ "-wgpeer",	NEXTARG,	A_WIREGUARD,	unsetwgpeer},
	{ "-wgpsk",	0,		A_WIREGUARD,	unsetwgpeerpsk},
	{ "-wgdescription", 0,		A_WIREGUARD,	unsetwgpeerdesc},
	{ "-wgdescr",	0,		A_WIREGUARD,	unsetwgpeerdesc},
	{ "-wgpeerall",	0,		A_WIREGUARD,	unsetwgpeerall},

#else /* SMALL */
	{ "powersave",	NEXTARG0,	0,		setignore },
	{ "priority",	NEXTARG,	0,		setignore },
	{ "rtlabel",	NEXTARG,	0,		setignore },
	{ "mpls",	IFXF_MPLS,	0,		setignore },
	{ "nwflag",	NEXTARG,	0,		setignore },
	{ "rdomain",	NEXTARG,	0,		setignore },
	{ "-inet",	AF_INET,	0,		removeaf },
	{ "-inet6",	AF_INET6,	0,		removeaf },
	{ "description", NEXTARG,	0,		setignore },
	{ "descr",	NEXTARG,	0,		setignore },
	{ "wol",	IFXF_WOL,	0,		setignore },
	{ "-wol",	-IFXF_WOL,	0,		setignore },
#endif /* SMALL */
#if 0
	/* XXX `create' special-cased below */
	{ "create",	0,		0,		clone_create } ,
#endif
	{ "destroy",	0,		0,		clone_destroy } ,
	{ "link0",	IFF_LINK0,	0,		setifflags } ,
	{ "-link0",	-IFF_LINK0,	0,		setifflags } ,
	{ "link1",	IFF_LINK1,	0,		setifflags } ,
	{ "-link1",	-IFF_LINK1,	0,		setifflags } ,
	{ "link2",	IFF_LINK2,	0,		setifflags } ,
	{ "-link2",	-IFF_LINK2,	0,		setifflags } ,
	{ "media",	NEXTARG0,	A_MEDIA,	setmedia },
	{ "mediaopt",	NEXTARG,	A_MEDIAOPTSET,	setmediaopt },
	{ "-mediaopt",	NEXTARG,	A_MEDIAOPTCLR,	unsetmediaopt },
	{ "mode",	NEXTARG,	A_MEDIAMODE,	setmediamode },
	{ "-mode",	0,		A_MEDIAMODE,	unsetmediamode },
	{ "instance",	NEXTARG,	A_MEDIAINST,	setmediainst },
	{ "inst",	NEXTARG,	A_MEDIAINST,	setmediainst },
	{ "lladdr",	NEXTARG,	0,		setiflladdr },
	{ "llprio",	NEXTARG,	0,		setifllprio },
	{ NULL, /*src*/	0,		0,		setifaddr },
	{ NULL, /*dst*/	0,		0,		setifdstaddr },
	{ NULL, /*illegal*/0,		0,		NULL },
};

#define	IFFBITS								\
	"\024\1UP\2BROADCAST\3DEBUG\4LOOPBACK\5POINTOPOINT\6STATICARP"	\
	"\7RUNNING\10NOARP\11PROMISC\12ALLMULTI\13OACTIVE\14SIMPLEX"	\
	"\15LINK0\16LINK1\17LINK2\20MULTICAST"				\
	"\23AUTOCONF6TEMP\24MPLS\25WOL\26AUTOCONF6\27INET6_NOSOII"	\
	"\30AUTOCONF4" "\31MONITOR" "\32LRO"

int	getinfo(struct ifreq *, int);
void	getsock(int);
void	printgroupattribs(char *);
void	printif(char *, int);
void	printb_status(unsigned short, unsigned char *);
const char *get_linkstate(int, int);
void	status(int, struct sockaddr_dl *, int, int);
__dead void	usage(void);
const char *get_string(const char *, const char *, u_int8_t *, int *);
int	len_string(const u_int8_t *, int);
int	print_string(const u_int8_t *, int);
char	*sec2str(time_t);

const char *get_media_type_string(uint64_t);
const char *get_media_subtype_string(uint64_t);
uint64_t	get_media_mode(uint64_t, const char *);
uint64_t	get_media_subtype(uint64_t, const char *);
uint64_t	get_media_options(uint64_t, const char *);
uint64_t	lookup_media_word(const struct ifmedia_description *, uint64_t,
	    const char *);
void	print_media_word(uint64_t, int, int);
void	process_media_commands(void);
void	init_current_media(void);

void	process_join_commands(void);

void	process_wg_commands(void);

void	in_status(int);
void	in_getaddr(const char *, int);
void	in_getprefix(const char *, int);
void	in6_fillscopeid(struct sockaddr_in6 *);
void	in6_alias(struct in6_ifreq *);
void	in6_status(int);
void	in6_getaddr(const char *, int);
void	in6_getprefix(const char *, int);
void	ieee80211_status(void);
void	join_status(void);
void	ieee80211_listchans(void);
void	ieee80211_listnodes(void);
void	ieee80211_printnode(struct ieee80211_nodereq *);
u_int	getwpacipher(const char *);
void	print_cipherset(u_int32_t);
void	print_rsnprotocol(u_int, u_int);

void	spppauthinfo(struct sauthreq *, int);
void	spppdnsinfo(struct sdnsreq *);

/* Known address families */
const struct afswtch {
	char *af_name;
	short af_af;
	void (*af_status)(int);
	void (*af_getaddr)(const char *, int);
	void (*af_getprefix)(const char *, int);
	u_long af_difaddr;
	u_long af_aifaddr;
	caddr_t af_ridreq;
	caddr_t af_addreq;
} afs[] = {
#define C(x) ((caddr_t) &x)
	{ "inet", AF_INET, in_status, in_getaddr, in_getprefix,
	    SIOCDIFADDR, SIOCAIFADDR, C(ridreq), C(in_addreq) },
	{ "inet6", AF_INET6, in6_status, in6_getaddr, in6_getprefix,
	    SIOCDIFADDR_IN6, SIOCAIFADDR_IN6, C(in6_ridreq), C(in6_addreq) },
	{ 0,	0,	    0,		0 }
};

const struct afswtch *afp;	/*the address family being set or asked about*/

char joinname[IEEE80211_NWID_LEN];
size_t joinlen;
char nwidname[IEEE80211_NWID_LEN];
size_t nwidlen;

int ifaliases = 0;
int aflag = 0;

int
main(int argc, char *argv[])
{
	const struct afswtch *rafp = NULL;
	int create = 0;
	int Cflag = 0;
	int gflag = 0;
	int found_rulefile = 0;
	int i;

	/* If no args at all, print all interfaces.  */
	if (argc < 2) {
		/* no filesystem visibility */
		if (unveil("/", "") == -1)
			err(1, "unveil /");
		if (unveil(NULL, NULL) == -1)
			err(1, "unveil");
		aflag = 1;
		printif(NULL, 0);
		return (0);
	}
	argc--, argv++;
	if (*argv[0] == '-') {
		int nomore = 0;

		for (i = 1; argv[0][i]; i++) {
			switch (argv[0][i]) {
			case 'a':
				aflag = 1;
				nomore = 1;
				break;
			case 'A':
				aflag = 1;
				ifaliases = 1;
				nomore = 1;
				break;
			case 'g':
				gflag = 1;
				break;
			case 'C':
				Cflag = 1;
				nomore = 1;
				break;
			case 'M':
				if (argv[1] == NULL)
					usage();
				exit(findmac(argv[1]));
				break;
			default:
				usage();
				break;
			}
		}
		if (nomore == 0) {
			argc--, argv++;
			if (argc < 1)
				usage();
			if (strlcpy(ifname, *argv, sizeof(ifname)) >= IFNAMSIZ)
				errx(1, "interface name '%s' too long", *argv);
		}
	} else if (strlcpy(ifname, *argv, sizeof(ifname)) >= IFNAMSIZ)
		errx(1, "interface name '%s' too long", *argv);
	argc--, argv++;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "rulefile") == 0) {
			found_rulefile = 1;
			break;
		}
	}

	if (!found_rulefile) {
		if (unveil(_PATH_RESCONF, "r") == -1)
			err(1, "unveil %s", _PATH_RESCONF);
		if (unveil(_PATH_HOSTS, "r") == -1)
			err(1, "unveil %s", _PATH_HOSTS);
		if (unveil(_PATH_SERVICES, "r") == -1)
			err(1, "unveil %s", _PATH_SERVICES);
		if (unveil(NULL, NULL) == -1)
			err(1, "unveil");
	}

	if (argc > 0) {
		for (afp = rafp = afs; rafp->af_name; rafp++)
			if (strcmp(rafp->af_name, *argv) == 0) {
				afp = rafp;
				argc--;
				argv++;
				break;
			}
		rafp = afp;
		af = ifr.ifr_addr.sa_family = rafp->af_af;
	}
	if (Cflag) {
		if (argc > 0 || aflag)
			usage();
		list_cloners();
		return (0);
	}
	if (gflag) {
		if (argc == 0)
			printgroupattribs(ifname);
		else
			setgroupattribs(ifname, argc, argv);
		return (0);
	}
	(void) strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));

	/* initialization */
	in6_addreq.ifra_lifetime.ia6t_pltime = ND6_INFINITE_LIFETIME;
	in6_addreq.ifra_lifetime.ia6t_vltime = ND6_INFINITE_LIFETIME;

	/*
	 * NOTE:  We must special-case the `create' command right
	 * here as we would otherwise fail in getinfo().
	 */
	if (argc > 0 && strcmp(argv[0], "create") == 0) {
		clone_create(argv[0], 0);
		argc--, argv++;
		if (argc == 0)
			return (0);
	}
	if (aflag == 0) {
		create = (argc > 0) && strcmp(argv[0], "destroy") != 0;
		(void)getinfo(&ifr, create);
	}

	if (argc != 0 && af == AF_INET6)
		addaf(ifname, AF_INET6);

	while (argc > 0) {
		const struct cmd *p;

		for (p = cmds; p->c_name; p++)
			if (strcmp(*argv, p->c_name) == 0)
				break;
#ifndef SMALL
		if (strcmp(*argv, "rule") == 0) {
			argc--, argv++;
			return bridge_rule(argc, argv, -1);
		}
#endif
		if (p->c_name == 0 && setaddr)
			for (i = setaddr; i > 0; i--) {
				p++;
				if (p->c_func == NULL)
					errx(1, "%s: bad value", *argv);
			}
		if (p->c_func || p->c_func2) {
			if (p->c_parameter == NEXTARG0) {
				const struct cmd *p0;
				int noarg = 1;

				if (argv[1]) {
					for (p0 = cmds; p0->c_name; p0++)
						if (strcmp(argv[1],
						    p0->c_name) == 0) {
							noarg = 0;
							break;
						}
				} else
					noarg = 0;

				if (noarg == 0)
					(*p->c_func)(NULL, 0);
				else
					goto nextarg;
			} else if (p->c_parameter == NEXTARG) {
nextarg:
				if (argv[1] == NULL)
					errx(1, "'%s' requires argument",
					    p->c_name);
				(*p->c_func)(argv[1], 0);
				argc--, argv++;
				actions = actions | A_SILENT | p->c_action;
			} else if (p->c_parameter == NEXTARG2) {
				if ((argv[1] == NULL) ||
				    (argv[2] == NULL))
					errx(1, "'%s' requires 2 arguments",
					    p->c_name);
				(*p->c_func2)(argv[1], argv[2]);
				argc -= 2;
				argv += 2;
				actions = actions | A_SILENT | p->c_action;
			} else {
				(*p->c_func)(*argv, p->c_parameter);
				actions = actions | A_SILENT | p->c_action;
			}
		}
		argc--, argv++;
	}

	if (argc == 0 && actions == 0) {
		printif(ifr.ifr_name, aflag ? ifaliases : 1);
		return (0);
	}

#ifndef SMALL
	process_wg_commands();
#endif

	process_join_commands();

	/* Process any media commands that may have been issued. */
	process_media_commands();

	if (af == AF_INET6 && explicit_prefix == 0) {
		/*
		 * Aggregatable address architecture defines all prefixes
		 * are 64. So, it is convenient to set prefixlen to 64 if
		 * it is not specified. If we are setting a destination
		 * address on a point-to-point interface, 128 is required.
		 */
		if (setipdst && (flags & IFF_POINTOPOINT))
			setifprefixlen("128", 0);
		else
			setifprefixlen("64", 0);
		/* in6_getprefix("64", MASK) if MASK is available here... */
	}

	if (clearaddr) {
		(void) strlcpy(rafp->af_ridreq, ifname, sizeof(ifr.ifr_name));
		if (ioctl(sock, rafp->af_difaddr, rafp->af_ridreq) == -1) {
			if (errno == EADDRNOTAVAIL && (doalias >= 0)) {
				/* means no previous address for interface */
			} else
				err(1, "SIOCDIFADDR");
		}
	}
	if (newaddr) {
		(void) strlcpy(rafp->af_addreq, ifname, sizeof(ifr.ifr_name));
		if (ioctl(sock, rafp->af_aifaddr, rafp->af_addreq) == -1)
			err(1, "SIOCAIFADDR");
	}
	return (0);
}

void
getsock(int naf)
{
	static int oaf = -1;

	if (oaf == naf)
		return;
	if (oaf != -1)
		close(sock);
	sock = socket(naf, SOCK_DGRAM, 0);
	if (sock == -1)
		oaf = -1;
	else
		oaf = naf;
}

int
getinfo(struct ifreq *ifr, int create)
{

	getsock(af);
	if (sock == -1)
		err(1, "socket");
	if (!isdigit((unsigned char)ifname[strlen(ifname) - 1]))
		return (-1);	/* ignore groups here */
	if (ioctl(sock, SIOCGIFFLAGS, (caddr_t)ifr) == -1) {
		int oerrno = errno;

		if (!create)
			return (-1);
		if (ioctl(sock, SIOCIFCREATE, (caddr_t)ifr) == -1) {
			errno = oerrno;
			return (-1);
		}
		if (ioctl(sock, SIOCGIFFLAGS, (caddr_t)ifr) == -1)
			return (-1);
	}
	flags = ifr->ifr_flags & 0xffff;
	if (ioctl(sock, SIOCGIFXFLAGS, (caddr_t)ifr) == -1)
		ifr->ifr_flags = 0;
	xflags = ifr->ifr_flags;
	if (ioctl(sock, SIOCGIFMETRIC, (caddr_t)ifr) == -1)
		metric = 0;
	else
		metric = ifr->ifr_metric;
	if (ioctl(sock, SIOCGIFMTU, (caddr_t)ifr) == -1)
		mtu = 0;
	else
		mtu = ifr->ifr_mtu;
#ifndef SMALL
	if (ioctl(sock, SIOCGIFRDOMAIN, (caddr_t)ifr) == -1)
		rdomainid = 0;
	else
		rdomainid = ifr->ifr_rdomainid;
#endif
	if (ioctl(sock, SIOCGIFLLPRIO, (caddr_t)ifr) == -1)
		llprio = 0;
	else
		llprio = ifr->ifr_llprio;

	return (0);
}

int
printgroup(char *groupname, int ifaliases)
{
	struct ifgroupreq	 ifgr;
	struct ifg_req		*ifg;
	int			 len, cnt = 0;

	getsock(AF_INET);
	bzero(&ifgr, sizeof(ifgr));
	strlcpy(ifgr.ifgr_name, groupname, sizeof(ifgr.ifgr_name));
	if (ioctl(sock, SIOCGIFGMEMB, (caddr_t)&ifgr) == -1) {
		if (errno == EINVAL || errno == ENOTTY ||
		    errno == ENOENT)
			return (-1);
		else
			err(1, "%s: SIOCGIFGMEMB", ifgr.ifgr_name);
	}

	len = ifgr.ifgr_len;
	if ((ifgr.ifgr_groups = calloc(1, len)) == NULL)
		err(1, "printgroup");
	if (ioctl(sock, SIOCGIFGMEMB, (caddr_t)&ifgr) == -1)
		err(1, "%s: SIOCGIFGMEMB", ifgr.ifgr_name);

	for (ifg = ifgr.ifgr_groups; ifg && len >= sizeof(struct ifg_req);
	    ifg++) {
		len -= sizeof(struct ifg_req);
		printif(ifg->ifgrq_member, ifaliases);
		cnt++;
	}
	free(ifgr.ifgr_groups);

	return (cnt);
}

void
printgroupattribs(char *groupname)
{
	struct ifgroupreq	 ifgr;

	getsock(AF_INET);
	bzero(&ifgr, sizeof(ifgr));
	strlcpy(ifgr.ifgr_name, groupname, sizeof(ifgr.ifgr_name));
	if (ioctl(sock, SIOCGIFGATTR, (caddr_t)&ifgr) == -1)
		err(1, "%s: SIOCGIFGATTR", ifgr.ifgr_name);

	printf("%s:", groupname);
	printf(" carp demote count %d", ifgr.ifgr_attrib.ifg_carp_demoted);
	printf("\n");
}

void
setgroupattribs(char *groupname, int argc, char *argv[])
{
	const char *errstr;
	char *p = argv[0];
	int neg = 1;

	struct ifgroupreq	 ifgr;

	getsock(AF_INET);
	bzero(&ifgr, sizeof(ifgr));
	strlcpy(ifgr.ifgr_name, groupname, sizeof(ifgr.ifgr_name));

	if (argc > 1) {
		neg = strtonum(argv[1], 0, 128, &errstr);
		if (errstr)
			errx(1, "%s: invalid carp demotion: %s", ifgr.ifgr_name,
			    errstr);
	}

	if (p[0] == '-') {
		neg = neg * -1;
		p++;
	}
	if (!strcmp(p, "carpdemote"))
		ifgr.ifgr_attrib.ifg_carp_demoted = neg;
	else
		usage();

	if (ioctl(sock, SIOCSIFGATTR, (caddr_t)&ifgr) == -1)
		err(1, "%s: SIOCSIFGATTR", ifgr.ifgr_name);
}

void
printif(char *name, int ifaliases)
{
	struct ifaddrs *ifap, *ifa;
	struct if_data *ifdata;
	const char *namep;
	char *oname = NULL;
	struct ifreq *ifrp;
	int count = 0, noinet = 1;
	size_t nlen = 0;

	if (aflag)
		name = NULL;
	if (name) {
		if ((oname = strdup(name)) == NULL)
			err(1, "strdup");
		nlen = strlen(oname);
		/* is it a group? */
		if (nlen && !isdigit((unsigned char)oname[nlen - 1]))
			if (printgroup(oname, ifaliases) != -1) {
				free(oname);
				return;
			}
	}

	if (getifaddrs(&ifap) != 0)
		err(1, "getifaddrs");

	namep = NULL;
	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		if (oname) {
			if (nlen && isdigit((unsigned char)oname[nlen - 1])) {
				/* must have exact match */
				if (strcmp(oname, ifa->ifa_name) != 0)
					continue;
			} else {
				/* partial match OK if it ends w/ digit */
				if (strncmp(oname, ifa->ifa_name, nlen) != 0 ||
				    !isdigit((unsigned char)ifa->ifa_name[nlen]))
					continue;
			}
		}
		/* quickhack: sizeof(ifr) < sizeof(ifr6) */
		if (ifa->ifa_addr != NULL &&
		    ifa->ifa_addr->sa_family == AF_INET6) {
			memset(&ifr6, 0, sizeof(ifr6));
			memcpy(&ifr6.ifr_addr, ifa->ifa_addr,
			    MINIMUM(sizeof(ifr6.ifr_addr), ifa->ifa_addr->sa_len));
			ifrp = (struct ifreq *)&ifr6;
		} else if (ifa->ifa_addr != NULL) {
			memset(&ifr, 0, sizeof(ifr));
			memcpy(&ifr.ifr_addr, ifa->ifa_addr,
			    MINIMUM(sizeof(ifr.ifr_addr), ifa->ifa_addr->sa_len));
			ifrp = &ifr;
		}
		strlcpy(ifname, ifa->ifa_name, sizeof(ifname));
		strlcpy(ifrp->ifr_name, ifa->ifa_name, sizeof(ifrp->ifr_name));

		if (ifa->ifa_addr != NULL &&
		    ifa->ifa_addr->sa_family == AF_LINK) {
			namep = ifa->ifa_name;
			if (getinfo(ifrp, 0) < 0)
				continue;
			ifdata = ifa->ifa_data;
			status(1, (struct sockaddr_dl *)ifa->ifa_addr,
			    ifdata->ifi_link_state, ifaliases);
			count++;
			noinet = 1;
			continue;
		}

		if (!namep || !strcmp(namep, ifa->ifa_name)) {
			const struct afswtch *p;

			if (ifa->ifa_addr == NULL ||
			    (ifa->ifa_addr->sa_family == AF_INET &&
			    ifaliases == 0 && noinet == 0))
				continue;
			if ((p = afp) != NULL) {
				if (ifa->ifa_addr->sa_family == p->af_af)
					p->af_status(1);
			} else {
				for (p = afs; p->af_name; p++) {
					if (ifa->ifa_addr->sa_family ==
					    p->af_af)
						p->af_status(0);
				}
			}
			count++;
			if (ifa->ifa_addr->sa_family == AF_INET)
				noinet = 0;
			continue;
		}
	}
	freeifaddrs(ifap);
	free(oname);
	if (count == 0) {
		fprintf(stderr, "%s: no such interface\n", ifname);
		exit(1);
	}
}

void
clone_create(const char *addr, int param)
{

	/* We're called early... */
	getsock(AF_INET);

	(void) strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	if (ioctl(sock, SIOCIFCREATE, &ifr) == -1)
		err(1, "%s: SIOCIFCREATE", ifr.ifr_name);
}

void
clone_destroy(const char *addr, int param)
{

	(void) strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	if (ioctl(sock, SIOCIFDESTROY, &ifr) == -1)
		err(1, "%s: SIOCIFDESTROY", ifr.ifr_name);
}

struct if_clonereq *
get_cloners(void)
{
	static struct if_clonereq ifcr;

	memset(&ifcr, 0, sizeof(ifcr));

	getsock(AF_INET);

	if (ioctl(sock, SIOCIFGCLONERS, &ifcr) == -1)
		err(1, "SIOCIFGCLONERS for count");

	if ((ifcr.ifcr_buffer = calloc(ifcr.ifcr_total, IFNAMSIZ)) == NULL)
		err(1, "unable to allocate cloner name buffer");
	ifcr.ifcr_count = ifcr.ifcr_total;

	if (ioctl(sock, SIOCIFGCLONERS, &ifcr) == -1)
		err(1, "SIOCIFGCLONERS for names");

	/*
	 * In case some disappeared in the mean time, clamp it down.
	 */
	if (ifcr.ifcr_count > ifcr.ifcr_total)
		ifcr.ifcr_count = ifcr.ifcr_total;

	return &ifcr;
}

void
list_cloners(void)
{
	struct if_clonereq *ifcr;
	char *cp, *buf;
	int idx;

	ifcr = get_cloners();
	buf = ifcr->ifcr_buffer;

	qsort(buf, ifcr->ifcr_count, IFNAMSIZ,
	    (int(*)(const void *, const void *))strcmp);

	for (cp = buf, idx = 0; idx < ifcr->ifcr_count; idx++, cp += IFNAMSIZ) {
		if (idx > 0)
			putchar(' ');
		printf("%s", cp);
	}

	putchar('\n');
	free(ifcr->ifcr_buffer);
}

#define RIDADDR 0
#define ADDR	1
#define MASK	2
#define DSTADDR	3

void
setifaddr(const char *addr, int param)
{
	/*
	 * Delay the ioctl to set the interface addr until flags are all set.
	 * The address interpretation may depend on the flags,
	 * and the flags may change when the address is set.
	 */
	setaddr++;
	if (doalias >= 0)
		newaddr = 1;
	if (doalias == 0)
		clearaddr = 1;
	afp->af_getaddr(addr, (doalias >= 0 ? ADDR : RIDADDR));
}

#ifndef SMALL
void
setifrtlabel(const char *label, int d)
{
	if (d != 0)
		ifr.ifr_data = (caddr_t)(const char *)"";
	else
		ifr.ifr_data = (caddr_t)label;
	if (ioctl(sock, SIOCSIFRTLABEL, &ifr) == -1)
		warn("SIOCSIFRTLABEL");
}
#endif

void
setifnetmask(const char *addr, int ignored)
{
	afp->af_getaddr(addr, MASK);
	explicit_prefix = 1;
}

void
setifbroadaddr(const char *addr, int ignored)
{
	afp->af_getaddr(addr, DSTADDR);
}

#ifndef SMALL
void
setifdesc(const char *val, int ignored)
{
	ifr.ifr_data = (caddr_t)val;
	if (ioctl(sock, SIOCSIFDESCR, &ifr) == -1)
		warn("SIOCSIFDESCR");
}

void
unsetifdesc(const char *noval, int ignored)
{
	ifr.ifr_data = (caddr_t)(const char *)"";
	if (ioctl(sock, SIOCSIFDESCR, &ifr) == -1)
		warn("SIOCSIFDESCR");
}

void
setifipdst(const char *addr, int ignored)
{
	in_getaddr(addr, DSTADDR);
	setipdst++;
	clearaddr = 0;
	newaddr = 0;
}
#endif

#define rqtosa(x) (&(((struct ifreq *)(afp->x))->ifr_addr))
void
notealias(const char *addr, int param)
{
	if (setaddr && doalias == 0 && param < 0)
		memcpy(rqtosa(af_ridreq), rqtosa(af_addreq),
		    rqtosa(af_addreq)->sa_len);
	doalias = param;
	if (param < 0) {
		clearaddr = 1;
		newaddr = 0;
	} else
		clearaddr = 0;
}

void
setifdstaddr(const char *addr, int param)
{
	setaddr++;
	setipdst++;
	afp->af_getaddr(addr, DSTADDR);
}

/*
 * Note: doing an SIOCGIFFLAGS scribbles on the union portion
 * of the ifreq structure, which may confuse other parts of ifconfig.
 * Make a private copy so we can avoid that.
 */
void
setifflags(const char *vname, int value)
{
	struct ifreq my_ifr;

	bcopy((char *)&ifr, (char *)&my_ifr, sizeof(struct ifreq));

	if (ioctl(sock, SIOCGIFFLAGS, (caddr_t)&my_ifr) == -1)
		err(1, "%s: SIOCGIFFLAGS", my_ifr.ifr_name);
	(void) strlcpy(my_ifr.ifr_name, ifname, sizeof(my_ifr.ifr_name));
	flags = my_ifr.ifr_flags;

	if (value < 0) {
		value = -value;
		flags &= ~value;
	} else
		flags |= value;
	my_ifr.ifr_flags = flags;
	if (ioctl(sock, SIOCSIFFLAGS, (caddr_t)&my_ifr) == -1)
		err(1, "%s: SIOCSIFFLAGS", my_ifr.ifr_name);
}

void
setifxflags(const char *vname, int value)
{
	struct ifreq my_ifr;

	bcopy((char *)&ifr, (char *)&my_ifr, sizeof(struct ifreq));

	if (ioctl(sock, SIOCGIFXFLAGS, (caddr_t)&my_ifr) == -1)
		warn("%s: SIOCGIFXFLAGS", my_ifr.ifr_name);
	(void) strlcpy(my_ifr.ifr_name, ifname, sizeof(my_ifr.ifr_name));
	xflags = my_ifr.ifr_flags;

	if (value < 0) {
		value = -value;
		xflags &= ~value;
	} else
		xflags |= value;
	my_ifr.ifr_flags = xflags;
	if (ioctl(sock, SIOCSIFXFLAGS, (caddr_t)&my_ifr) == -1)
		warn("%s: SIOCSIFXFLAGS", my_ifr.ifr_name);
}

void
addaf(const char *vname, int value)
{
	struct if_afreq	ifar;

	strlcpy(ifar.ifar_name, ifname, sizeof(ifar.ifar_name));
	ifar.ifar_af = value;
	if (ioctl(sock, SIOCIFAFATTACH, (caddr_t)&ifar) == -1)
		warn("%s: SIOCIFAFATTACH", ifar.ifar_name);
}

void
removeaf(const char *vname, int value)
{
	struct if_afreq	ifar;

	strlcpy(ifar.ifar_name, ifname, sizeof(ifar.ifar_name));
	ifar.ifar_af = value;
	if (ioctl(sock, SIOCIFAFDETACH, (caddr_t)&ifar) == -1)
		warn("%s: SIOCIFAFDETACH", ifar.ifar_name);
}

void
setia6flags(const char *vname, int value)
{

	if (value < 0) {
		value = -value;
		in6_addreq.ifra_flags &= ~value;
	} else
		in6_addreq.ifra_flags |= value;
}

void
setia6pltime(const char *val, int d)
{

	setia6lifetime("pltime", val);
}

void
setia6vltime(const char *val, int d)
{

	setia6lifetime("vltime", val);
}

void
setia6lifetime(const char *cmd, const char *val)
{
	const char *errmsg = NULL;
	time_t newval, t;

	newval = strtonum(val, 0, 1000000, &errmsg);
	if (errmsg)
		errx(1, "invalid %s %s: %s", cmd, val, errmsg);

	t = time(NULL);

	if (afp->af_af != AF_INET6)
		errx(1, "%s not allowed for this address family", cmd);
	if (strcmp(cmd, "vltime") == 0) {
		in6_addreq.ifra_lifetime.ia6t_expire = t + newval;
		in6_addreq.ifra_lifetime.ia6t_vltime = newval;
	} else if (strcmp(cmd, "pltime") == 0) {
		in6_addreq.ifra_lifetime.ia6t_preferred = t + newval;
		in6_addreq.ifra_lifetime.ia6t_pltime = newval;
	}
}

void
setia6eui64(const char *cmd, int val)
{
	struct ifaddrs *ifap, *ifa;
	const struct sockaddr_in6 *sin6 = NULL;
	const struct in6_addr *lladdr = NULL;
	struct in6_addr *in6;

	if (afp->af_af != AF_INET6)
		errx(1, "%s not allowed for this address family", cmd);

	addaf(ifname, AF_INET6);

	in6 = (struct in6_addr *)&in6_addreq.ifra_addr.sin6_addr;
	if (memcmp(&in6addr_any.s6_addr[8], &in6->s6_addr[8], 8) != 0)
		errx(1, "interface index is already filled");
	if (getifaddrs(&ifap) != 0)
		err(1, "getifaddrs");
	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr->sa_family == AF_INET6 &&
		    strcmp(ifa->ifa_name, ifname) == 0) {
			sin6 = (const struct sockaddr_in6 *)ifa->ifa_addr;
			if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr)) {
				lladdr = &sin6->sin6_addr;
				break;
			}
		}
	}
	if (!lladdr)
		errx(1, "could not determine link local address");

	memcpy(&in6->s6_addr[8], &lladdr->s6_addr[8], 8);

	freeifaddrs(ifap);
}

void
setautoconf(const char *cmd, int val)
{
	switch (afp->af_af) {
	case AF_INET:
		setifxflags("inet", val * IFXF_AUTOCONF4);
		break;
	case AF_INET6:
		if (val > 0)
			setifxflags("inet6", (IFXF_AUTOCONF6 |
			    IFXF_AUTOCONF6TEMP));
		else
			setifxflags("inet6", -IFXF_AUTOCONF6);
		break;
	default:
		errx(1, "autoconf not allowed for this address family");
	}
}

void
settemporary(const char *cmd, int val)
{
	switch (afp->af_af) {
	case AF_INET6:
		setifxflags("inet6", val * IFXF_AUTOCONF6TEMP);
		break;
	default:
		errx(1, "temporary not allowed for this address family");
	}
}

#ifndef SMALL
void
setifmetric(const char *val, int ignored)
{
	const char *errmsg = NULL;

	(void) strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));

	ifr.ifr_metric = strtonum(val, 0, INT_MAX, &errmsg);
	if (errmsg)
		errx(1, "metric %s: %s", val, errmsg);
	if (ioctl(sock, SIOCSIFMETRIC, (caddr_t)&ifr) == -1)
		warn("SIOCSIFMETRIC");
}
#endif

void
setifmtu(const char *val, int d)
{
	const char *errmsg = NULL;

	(void) strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));

	ifr.ifr_mtu = strtonum(val, 0, INT_MAX, &errmsg);
	if (errmsg)
		errx(1, "mtu %s: %s", val, errmsg);
	if (ioctl(sock, SIOCSIFMTU, (caddr_t)&ifr) == -1)
		warn("SIOCSIFMTU");
}

void
setifllprio(const char *val, int d)
{
	const char *errmsg = NULL;

	(void) strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));

	ifr.ifr_llprio = strtonum(val, 0, UCHAR_MAX, &errmsg);
	if (errmsg)
		errx(1, "llprio %s: %s", val, errmsg);
	if (ioctl(sock, SIOCSIFLLPRIO, (caddr_t)&ifr) == -1)
		warn("SIOCSIFLLPRIO");
}

void
setifgroup(const char *group_name, int dummy)
{
	struct ifgroupreq ifgr;
	size_t namelen;

	memset(&ifgr, 0, sizeof(ifgr));
	strlcpy(ifgr.ifgr_name, ifname, IFNAMSIZ);

	namelen = strlen(group_name);
	if (namelen == 0)
		errx(1, "setifgroup: group name empty");
	if (namelen >= IFNAMSIZ)
		errx(1, "setifgroup: group name too long");
	if (isdigit((unsigned char)group_name[namelen - 1]))
		errx(1, "setifgroup: group names may not end in a digit");

	strlcpy(ifgr.ifgr_group, group_name, IFNAMSIZ);
	if (ioctl(sock, SIOCAIFGROUP, (caddr_t)&ifgr) == -1) {
		if (errno != EEXIST)
			err(1,"%s: SIOCAIFGROUP", group_name);
	}
}

void
unsetifgroup(const char *group_name, int dummy)
{
	struct ifgroupreq ifgr;

	memset(&ifgr, 0, sizeof(ifgr));
	strlcpy(ifgr.ifgr_name, ifname, IFNAMSIZ);

	if (strlcpy(ifgr.ifgr_group, group_name, IFNAMSIZ) >= IFNAMSIZ)
		errx(1, "unsetifgroup: group name too long");
	if (ioctl(sock, SIOCDIFGROUP, (caddr_t)&ifgr) == -1)
		err(1, "%s: SIOCDIFGROUP", group_name);
}

const char *
get_string(const char *val, const char *sep, u_int8_t *buf, int *lenp)
{
	int len = *lenp, hexstr;
	u_int8_t *p = buf;

	hexstr = (val[0] == '0' && tolower((u_char)val[1]) == 'x');
	if (hexstr)
		val += 2;
	for (;;) {
		if (*val == '\0')
			break;
		if (sep != NULL && strchr(sep, *val) != NULL) {
			val++;
			break;
		}
		if (hexstr) {
			if (!isxdigit((u_char)val[0]) ||
			    !isxdigit((u_char)val[1])) {
				warnx("bad hexadecimal digits");
				return NULL;
			}
		}
		if (p > buf + len) {
			if (hexstr)
				warnx("hexadecimal digits too long");
			else
				warnx("strings too long");
			return NULL;
		}
		if (hexstr) {
#define	tohex(x)	(isdigit(x) ? (x) - '0' : tolower(x) - 'a' + 10)
			*p++ = (tohex((u_char)val[0]) << 4) |
			    tohex((u_char)val[1]);
#undef tohex
			val += 2;
		} else {
			if (*val == '\\' &&
			    sep != NULL && strchr(sep, *(val + 1)) != NULL)
				val++;
			*p++ = *val++;
		}
	}
	len = p - buf;
	if (len < *lenp)
		memset(p, 0, *lenp - len);
	*lenp = len;
	return val;
}

int
len_string(const u_int8_t *buf, int len)
{
	int i = 0, hasspc = 0;

	if (len < 2 || buf[0] != '0' || tolower(buf[1]) != 'x') {
		for (; i < len; i++) {
			/* Only print 7-bit ASCII keys */
			if (buf[i] & 0x80 || !isprint(buf[i]))
				break;
			if (isspace(buf[i]))
				hasspc++;
		}
	}
	if (i == len) {
		if (hasspc || len == 0)
			return len + 2;
		else
			return len;
	} else
		return (len * 2) + 2;
}

int
print_string(const u_int8_t *buf, int len)
{
	int i = 0, hasspc = 0;

	if (len < 2 || buf[0] != '0' || tolower(buf[1]) != 'x') {
		for (; i < len; i++) {
			/* Only print 7-bit ASCII keys */
			if (buf[i] & 0x80 || !isprint(buf[i]))
				break;
			if (isspace(buf[i]))
				hasspc++;
		}
	}
	if (i == len) {
		if (hasspc || len == 0) {
			printf("\"%.*s\"", len, buf);
			return len + 2;
		} else {
			printf("%.*s", len, buf);
			return len;
		}
	} else {
		printf("0x");
		for (i = 0; i < len; i++)
			printf("%02x", buf[i]);
		return (len * 2) + 2;
	}
}

void
setifnwid(const char *val, int d)
{
	struct ieee80211_nwid nwid;
	int len;

	if (joinlen != 0) {
		errx(1, "nwid and join may not be used at the same time");
	}

	if (nwidlen != 0) {
		errx(1, "nwid may not be specified twice");
	}

	if (d != 0) {
		/* no network id is especially desired */
		memset(&nwid, 0, sizeof(nwid));
		len = 0;
	} else {
		len = sizeof(nwid.i_nwid);
		if (get_string(val, NULL, nwid.i_nwid, &len) == NULL)
			return;
	}
	nwidlen = nwid.i_len = len;
	(void)strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	memcpy(nwidname, nwid.i_nwid, len);
	ifr.ifr_data = (caddr_t)&nwid;
	if (ioctl(sock, SIOCS80211NWID, (caddr_t)&ifr) == -1)
		warn("SIOCS80211NWID");
}


void
process_join_commands(void)
{
	if (!(actions & A_JOIN))
		return;

	ifr.ifr_data = (caddr_t)&join;
	if (ioctl(sock, SIOCS80211JOIN, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCS80211JOIN", ifr.ifr_name);
}

void
setifjoin(const char *val, int d)
{
	int len;

	if (nwidlen != 0) {
		errx(1, "nwid and join may not be used at the same time");
	}

	if (joinlen != 0) {
		errx(1, "join may not be specified twice");
	}

	if (d != 0) {
		/* no network id is especially desired */
		memset(&join, 0, sizeof(join));
		len = 0;
	} else {
		len = sizeof(join.i_nwid);
		if (get_string(val, NULL, join.i_nwid, &len) == NULL)
			return;
		if (len == 0)
			join.i_flags |= IEEE80211_JOIN_ANY;
	}
	joinlen = join.i_len = len;
	(void)strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	memcpy(joinname, join.i_nwid, len);

	actions |= A_JOIN;
}

void
delifjoin(const char *val, int d)
{
	struct ieee80211_join join;
	int len;

	memset(&join, 0, sizeof(join));
	len = 0;
	join.i_flags |= IEEE80211_JOIN_DEL;

	if (d == -1) {
		ifr.ifr_data = (caddr_t)&join;
		if (ioctl(sock, SIOCS80211JOIN, (caddr_t)&ifr) == -1)
			err(1, "%s: SIOCS80211JOIN", ifr.ifr_name);
	}

	len = sizeof(join.i_nwid);
	if (get_string(val, NULL, join.i_nwid, &len) == NULL)
		return;
	join.i_len = len;
	if (len == 0)
		join.i_flags |= IEEE80211_JOIN_ANY;
	(void)strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	ifr.ifr_data = (caddr_t)&join;
	if (ioctl(sock, SIOCS80211JOIN, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCS80211JOIN", ifr.ifr_name);
}

void
delifjoinlist(const char *val, int d)
{
	struct ieee80211_join join;

	memset(&join, 0, sizeof(join));
	join.i_flags |= (IEEE80211_JOIN_DEL | IEEE80211_JOIN_DEL_ALL);

	ifr.ifr_data = (caddr_t)&join;
	if (ioctl(sock, SIOCS80211JOIN, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCS80211JOIN", ifr.ifr_name);
}

void
setifbssid(const char *val, int d)
{

	struct ieee80211_bssid bssid;
	struct ether_addr *ea;

	if (d != 0) {
		/* no BSSID is especially desired */
		memset(&bssid.i_bssid, 0, sizeof(bssid.i_bssid));
	} else {
		ea = ether_aton((char*)val);
		if (ea == NULL) {
			warnx("malformed BSSID: %s", val);
			return;
		}
		memcpy(&bssid.i_bssid, ea->ether_addr_octet,
		    sizeof(bssid.i_bssid));
	}
	strlcpy(bssid.i_name, ifname, sizeof(bssid.i_name));
	if (ioctl(sock, SIOCS80211BSSID, &bssid) == -1)
		warn("%s: SIOCS80211BSSID", bssid.i_name);
}

void
setifnwkey(const char *val, int d)
{
	int i, len;
	struct ieee80211_nwkey nwkey;
	u_int8_t keybuf[IEEE80211_WEP_NKID][16];

	bzero(&nwkey, sizeof(nwkey));
	bzero(&keybuf, sizeof(keybuf));

	nwkey.i_wepon = IEEE80211_NWKEY_WEP;
	nwkey.i_defkid = 1;
	if (d == -1) {
		/* disable WEP encryption */
		nwkey.i_wepon = IEEE80211_NWKEY_OPEN;
		i = 0;
	} else if (strcasecmp("persist", val) == 0) {
		/* use all values from persistent memory */
		nwkey.i_wepon |= IEEE80211_NWKEY_PERSIST;
		nwkey.i_defkid = 0;
		for (i = 0; i < IEEE80211_WEP_NKID; i++)
			nwkey.i_key[i].i_keylen = -1;
	} else if (strncasecmp("persist:", val, 8) == 0) {
		val += 8;
		/* program keys in persistent memory */
		nwkey.i_wepon |= IEEE80211_NWKEY_PERSIST;
		goto set_nwkey;
	} else {
 set_nwkey:
		if (isdigit((unsigned char)val[0]) && val[1] == ':') {
			/* specifying a full set of four keys */
			nwkey.i_defkid = val[0] - '0';
			val += 2;
			for (i = 0; i < IEEE80211_WEP_NKID; i++) {
				len = sizeof(keybuf[i]);
				val = get_string(val, ",", keybuf[i], &len);
				if (val == NULL)
					return;
				nwkey.i_key[i].i_keylen = len;
				nwkey.i_key[i].i_keydat = keybuf[i];
			}
			if (*val != '\0') {
				warnx("SIOCS80211NWKEY: too many keys.");
				return;
			}
		} else {
			/*
			 * length of each key must be either a 5
			 * character ASCII string or 10 hex digits for
			 * 40 bit encryption, or 13 character ASCII
			 * string or 26 hex digits for 128 bit
			 * encryption.
			 */
			int j;
			char *tmp = NULL;
			size_t vlen = strlen(val);
			switch(vlen) {
			case 10:
			case 26:
				/* 0x must be missing for these lengths */
				j = asprintf(&tmp, "0x%s", val);
				if (j == -1) {
					warnx("malloc failed");
					return;
				}
				val = tmp;
				break;
			case 12:
			case 28:
			case 5:
			case 13:
				/* 0xkey or string case - all is ok */
				break;
			default:
				warnx("Invalid WEP key length");
				return;
			}
			len = sizeof(keybuf[0]);
			val = get_string(val, NULL, keybuf[0], &len);
			free(tmp);
			if (val == NULL)
				return;
			nwkey.i_key[0].i_keylen = len;
			nwkey.i_key[0].i_keydat = keybuf[0];
			i = 1;
		}
	}
	(void)strlcpy(nwkey.i_name, ifname, sizeof(nwkey.i_name));

	if (actions & A_JOIN) {
		memcpy(&join.i_nwkey, &nwkey, sizeof(join.i_nwkey));
		join.i_flags |= IEEE80211_JOIN_NWKEY;
		return;
	}

	if (ioctl(sock, SIOCS80211NWKEY, (caddr_t)&nwkey) == -1)
		err(1, "%s: SIOCS80211NWKEY", nwkey.i_name);
}

void
setifwpa(const char *val, int d)
{
	struct ieee80211_wpaparams wpa;

	memset(&wpa, 0, sizeof(wpa));
	(void)strlcpy(wpa.i_name, ifname, sizeof(wpa.i_name));
	/* Don't read current values. The kernel will set defaults. */
	wpa.i_enabled = d;

	if (actions & A_JOIN) {
		join.i_wpaparams.i_enabled = d;
		join.i_flags |= IEEE80211_JOIN_WPA;
		return;
	}

	if (ioctl(sock, SIOCS80211WPAPARMS, (caddr_t)&wpa) == -1)
		err(1, "%s: SIOCS80211WPAPARMS", wpa.i_name);
}

void
setifwpaprotos(const char *val, int d)
{
	struct ieee80211_wpaparams wpa;
	char *optlist, *str;
	u_int rval = 0;

	if ((optlist = strdup(val)) == NULL)
		err(1, "strdup");
	str = strtok(optlist, ",");
	while (str != NULL) {
		if (strcasecmp(str, "wpa1") == 0)
			rval |= IEEE80211_WPA_PROTO_WPA1;
		else if (strcasecmp(str, "wpa2") == 0)
			rval |= IEEE80211_WPA_PROTO_WPA2;
		else
			errx(1, "wpaprotos: unknown protocol: %s", str);
		str = strtok(NULL, ",");
	}
	free(optlist);

	if (actions & A_JOIN) {
		join.i_wpaparams.i_protos = rval;
		join.i_flags |= IEEE80211_JOIN_WPA;
		return;
	}

	memset(&wpa, 0, sizeof(wpa));
	(void)strlcpy(wpa.i_name, ifname, sizeof(wpa.i_name));
	if (ioctl(sock, SIOCG80211WPAPARMS, (caddr_t)&wpa) == -1)
		err(1, "%s: SIOCG80211WPAPARMS", wpa.i_name);
	wpa.i_protos = rval;
	/* Let the kernel set up the appropriate default ciphers. */
	wpa.i_ciphers = 0;
	wpa.i_groupcipher = 0;

	if (ioctl(sock, SIOCS80211WPAPARMS, (caddr_t)&wpa) == -1)
		err(1, "%s: SIOCS80211WPAPARMS", wpa.i_name);
}

void
setifwpaakms(const char *val, int d)
{
	struct ieee80211_wpaparams wpa;
	char *optlist, *str;
	u_int rval = 0;

	if ((optlist = strdup(val)) == NULL)
		err(1, "strdup");
	str = strtok(optlist, ",");
	while (str != NULL) {
		if (strcasecmp(str, "psk") == 0)
			rval |= IEEE80211_WPA_AKM_PSK;
		else if (strcasecmp(str, "802.1x") == 0)
			rval |= IEEE80211_WPA_AKM_8021X;
		else
			errx(1, "wpaakms: unknown akm: %s", str);
		str = strtok(NULL, ",");
	}
	free(optlist);

	if (actions & A_JOIN) {
		join.i_wpaparams.i_akms = rval;
		join.i_wpaparams.i_enabled =
		    ((rval & IEEE80211_WPA_AKM_8021X) != 0);
		join.i_flags |= IEEE80211_JOIN_WPA;
		return;
	}

	memset(&wpa, 0, sizeof(wpa));
	(void)strlcpy(wpa.i_name, ifname, sizeof(wpa.i_name));
	if (ioctl(sock, SIOCG80211WPAPARMS, (caddr_t)&wpa) == -1)
		err(1, "%s: SIOCG80211WPAPARMS", wpa.i_name);
	wpa.i_akms = rval;
	/* Enable WPA for 802.1x here. PSK case is handled in setifwpakey(). */
	wpa.i_enabled = ((rval & IEEE80211_WPA_AKM_8021X) != 0);

	if (ioctl(sock, SIOCS80211WPAPARMS, (caddr_t)&wpa) == -1)
		err(1, "%s: SIOCS80211WPAPARMS", wpa.i_name);
}

static const struct {
	const char	*name;
	u_int		cipher;
} ciphers[] = {
	{ "usegroup",	IEEE80211_WPA_CIPHER_USEGROUP },
	{ "wep40",	IEEE80211_WPA_CIPHER_WEP40 },
	{ "tkip",	IEEE80211_WPA_CIPHER_TKIP },
	{ "ccmp",	IEEE80211_WPA_CIPHER_CCMP },
	{ "wep104",	IEEE80211_WPA_CIPHER_WEP104 }
};

u_int
getwpacipher(const char *name)
{
	int i;

	for (i = 0; i < sizeof(ciphers) / sizeof(ciphers[0]); i++)
		if (strcasecmp(name, ciphers[i].name) == 0)
			return ciphers[i].cipher;
	return IEEE80211_WPA_CIPHER_NONE;
}

void
setifwpaciphers(const char *val, int d)
{
	struct ieee80211_wpaparams wpa;
	char *optlist, *str;
	u_int rval = 0;

	if ((optlist = strdup(val)) == NULL)
		err(1, "strdup");
	str = strtok(optlist, ",");
	while (str != NULL) {
		u_int cipher = getwpacipher(str);
		if (cipher == IEEE80211_WPA_CIPHER_NONE)
			errx(1, "wpaciphers: unknown cipher: %s", str);

		rval |= cipher;
		str = strtok(NULL, ",");
	}
	free(optlist);

	if (actions & A_JOIN) {
		join.i_wpaparams.i_ciphers = rval;
		join.i_flags |= IEEE80211_JOIN_WPA;
		return;
	}

	memset(&wpa, 0, sizeof(wpa));
	(void)strlcpy(wpa.i_name, ifname, sizeof(wpa.i_name));
	if (ioctl(sock, SIOCG80211WPAPARMS, (caddr_t)&wpa) == -1)
		err(1, "%s: SIOCG80211WPAPARMS", wpa.i_name);
	wpa.i_ciphers = rval;

	if (ioctl(sock, SIOCS80211WPAPARMS, (caddr_t)&wpa) == -1)
		err(1, "%s: SIOCS80211WPAPARMS", wpa.i_name);
}

void
setifwpagroupcipher(const char *val, int d)
{
	struct ieee80211_wpaparams wpa;
	u_int cipher;

	cipher = getwpacipher(val);
	if (cipher == IEEE80211_WPA_CIPHER_NONE)
		errx(1, "wpagroupcipher: unknown cipher: %s", val);

	memset(&wpa, 0, sizeof(wpa));
	(void)strlcpy(wpa.i_name, ifname, sizeof(wpa.i_name));
	if (ioctl(sock, SIOCG80211WPAPARMS, (caddr_t)&wpa) == -1)
		err(1, "%s: SIOCG80211WPAPARMS", wpa.i_name);
	wpa.i_groupcipher = cipher;

	if (actions & A_JOIN) {
		join.i_wpaparams.i_groupcipher = cipher;
		join.i_flags |= IEEE80211_JOIN_WPA;
		return;
	}

	if (ioctl(sock, SIOCS80211WPAPARMS, (caddr_t)&wpa) == -1)
		err(1, "%s: SIOCS80211WPAPARMS", wpa.i_name);
}

void
setifwpakey(const char *val, int d)
{
	struct ieee80211_wpaparams wpa;
	struct ieee80211_wpapsk psk;
	struct ieee80211_nwid nwid;
	int passlen;

	memset(&psk, 0, sizeof(psk));
	if (d != -1) {
		memset(&ifr, 0, sizeof(ifr));
		ifr.ifr_data = (caddr_t)&nwid;
		strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));

		/* Use the value specified in 'join' or 'nwid' */
		if (joinlen != 0) {
			memcpy(nwid.i_nwid, joinname, joinlen);
			nwid.i_len = joinlen;
		} else if (nwidlen != 0) {
			memcpy(nwid.i_nwid, nwidname, nwidlen);
			nwid.i_len = nwidlen;
		} else {
			warnx("no nwid or join command, guessing nwid to use");

			if (ioctl(sock, SIOCG80211NWID, (caddr_t)&ifr) == -1)
				err(1, "%s: SIOCG80211NWID", ifr.ifr_name);
		}

		passlen = strlen(val);
		if (passlen == 2 + 2 * sizeof(psk.i_psk) &&
		    val[0] == '0' && val[1] == 'x') {
			/* Parse a WPA hex key (must be full-length) */
			passlen = sizeof(psk.i_psk);
			val = get_string(val, NULL, psk.i_psk, &passlen);
			if (val == NULL || passlen != sizeof(psk.i_psk))
				errx(1, "wpakey: invalid pre-shared key");
		} else {
			/* Parse a WPA passphrase */
			if (passlen < 8 || passlen > 63)
				errx(1, "wpakey: passphrase must be between "
				    "8 and 63 characters");
			if (nwid.i_len == 0)
				errx(1, "wpakey: nwid not set");
			if (pkcs5_pbkdf2(val, passlen, nwid.i_nwid, nwid.i_len,
			    psk.i_psk, sizeof(psk.i_psk), 4096) != 0)
				errx(1, "wpakey: passphrase hashing failed");
		}
		psk.i_enabled = 1;
	} else
		psk.i_enabled = 0;

	(void)strlcpy(psk.i_name, ifname, sizeof(psk.i_name));

	if (actions & A_JOIN) {
		memcpy(&join.i_wpapsk, &psk, sizeof(join.i_wpapsk));
		join.i_flags |= IEEE80211_JOIN_WPAPSK;
		if (!join.i_wpaparams.i_enabled)
			setifwpa(NULL, join.i_wpapsk.i_enabled);
		return;
	}

	if (ioctl(sock, SIOCS80211WPAPSK, (caddr_t)&psk) == -1)
		err(1, "%s: SIOCS80211WPAPSK", psk.i_name);

	/* And ... automatically enable or disable WPA */
	memset(&wpa, 0, sizeof(wpa));
	(void)strlcpy(wpa.i_name, ifname, sizeof(wpa.i_name));
	if (ioctl(sock, SIOCG80211WPAPARMS, (caddr_t)&wpa) == -1)
		err(1, "%s: SIOCG80211WPAPARMS", psk.i_name);
	wpa.i_enabled = psk.i_enabled;
	if (ioctl(sock, SIOCS80211WPAPARMS, (caddr_t)&wpa) == -1)
		err(1, "%s: SIOCS80211WPAPARMS", psk.i_name);
}

void
setifchan(const char *val, int d)
{
	struct ieee80211chanreq channel;
	const char *errstr;
	int chan;

	if (val == NULL) {
		if (shownet80211chans || shownet80211nodes)
			usage();
		shownet80211chans = 1;
		return;
	}
	if (d != 0)
		chan = IEEE80211_CHAN_ANY;
	else {
		chan = strtonum(val, 1, 256, &errstr);
		if (errstr) {
			warnx("invalid channel %s: %s", val, errstr);
			return;
		}
	}

	strlcpy(channel.i_name, ifname, sizeof(channel.i_name));
	channel.i_channel = (u_int16_t)chan;
	if (ioctl(sock, SIOCS80211CHANNEL, (caddr_t)&channel) == -1)
		warn("%s: SIOCS80211CHANNEL", channel.i_name);
}

void
setifscan(const char *val, int d)
{
	if (shownet80211chans || shownet80211nodes)
		usage();
	shownet80211nodes = 1;
}

#ifndef SMALL

void
setifnwflag(const char *val, int d)
{
	static const struct ieee80211_flags nwflags[] = IEEE80211_FLAGS;
	u_int i, flag = 0;

	for (i = 0; i < (sizeof(nwflags) / sizeof(nwflags[0])); i++) {
		if (strcmp(val, nwflags[i].f_name) == 0) {
			flag = nwflags[i].f_flag;
			break;
		}
	}
	if (flag == 0)
		errx(1, "Invalid nwflag: %s", val);

	if (ioctl(sock, SIOCG80211FLAGS, (caddr_t)&ifr) != 0)
		err(1, "%s: SIOCG80211FLAGS", ifr.ifr_name);

	if (d)
		ifr.ifr_flags &= ~flag;
	else
		ifr.ifr_flags |= flag;

	if (ioctl(sock, SIOCS80211FLAGS, (caddr_t)&ifr) != 0)
		err(1, "%s: SIOCS80211FLAGS", ifr.ifr_name);
}

void
unsetifnwflag(const char *val, int d)
{
	setifnwflag(val, 1);
}

void
setifpowersave(const char *val, int d)
{
	struct ieee80211_power power;
	const char *errmsg = NULL;

	(void)strlcpy(power.i_name, ifname, sizeof(power.i_name));
	if (ioctl(sock, SIOCG80211POWER, (caddr_t)&power) == -1) {
		warn("%s: SIOCG80211POWER", power.i_name);
		return;
	}

	if (d != -1 && val != NULL) {
		power.i_maxsleep = strtonum(val, 0, INT_MAX, &errmsg);
		if (errmsg)
			errx(1, "powersave %s: %s", val, errmsg);
	}

	power.i_enabled = d == -1 ? 0 : 1;
	if (ioctl(sock, SIOCS80211POWER, (caddr_t)&power) == -1)
		warn("%s: SIOCS80211POWER", power.i_name);
}
#endif

void
print_cipherset(u_int32_t cipherset)
{
	const char *sep = "";
	int i;

	if (cipherset == IEEE80211_WPA_CIPHER_NONE) {
		printf("none");
		return;
	}
	for (i = 0; i < sizeof(ciphers) / sizeof(ciphers[0]); i++) {
		if (cipherset & ciphers[i].cipher) {
			printf("%s%s", sep, ciphers[i].name);
			sep = ",";
		}
	}
}

void
print_rsnprotocol(u_int proto, u_int akm)
{
	if (proto & IEEE80211_WPA_PROTO_WPA2) {
		if (akm & IEEE80211_WPA_AKM_SAE) {
			if (akm == IEEE80211_WPA_AKM_SAE)
				fputs(",wpa3", stdout);
			else
				fputs(",wpa3,wpa2", stdout);
		} else
			fputs(",wpa2", stdout);
	}
	if (proto & IEEE80211_WPA_PROTO_WPA1)
		fputs(",wpa1", stdout);
}

static void
print_assoc_failures(uint32_t assoc_fail)
{
	/* Filter out the most obvious failure cases. */
	assoc_fail &= ~IEEE80211_NODEREQ_ASSOCFAIL_ESSID;
	if (assoc_fail & IEEE80211_NODEREQ_ASSOCFAIL_PRIVACY)
		assoc_fail &= ~IEEE80211_NODEREQ_ASSOCFAIL_WPA_PROTO;
	assoc_fail &= ~IEEE80211_NODEREQ_ASSOCFAIL_PRIVACY;

	if (assoc_fail == 0)
		return;

	printb_status(assoc_fail, IEEE80211_NODEREQ_ASSOCFAIL_BITS);
}

void
ieee80211_status(void)
{
	int len, inwid, ijoin, inwkey, ipsk, ichan, ipwr;
	int ibssid, iwpa, assocfail = 0;
	struct ieee80211_nwid nwid;
	struct ieee80211_join join;
	struct ieee80211_nwkey nwkey;
	struct ieee80211_wpapsk psk;
	struct ieee80211_power power;
	struct ieee80211chanreq channel;
	struct ieee80211_bssid bssid;
	struct ieee80211_wpaparams wpa;
	struct ieee80211_nodereq nr;
	u_int8_t zero_bssid[IEEE80211_ADDR_LEN];
	struct ether_addr ea;

	/* get current status via ioctls */
	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_data = (caddr_t)&nwid;
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	inwid = ioctl(sock, SIOCG80211NWID, (caddr_t)&ifr);

	ifr.ifr_data = (caddr_t)&join;
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	ijoin = ioctl(sock, SIOCG80211JOIN, (caddr_t)&ifr);

	memset(&nwkey, 0, sizeof(nwkey));
	strlcpy(nwkey.i_name, ifname, sizeof(nwkey.i_name));
	inwkey = ioctl(sock, SIOCG80211NWKEY, (caddr_t)&nwkey);

	memset(&psk, 0, sizeof(psk));
	strlcpy(psk.i_name, ifname, sizeof(psk.i_name));
	ipsk = ioctl(sock, SIOCG80211WPAPSK, (caddr_t)&psk);

	memset(&power, 0, sizeof(power));
	strlcpy(power.i_name, ifname, sizeof(power.i_name));
	ipwr = ioctl(sock, SIOCG80211POWER, &power);

	memset(&channel, 0, sizeof(channel));
	strlcpy(channel.i_name, ifname, sizeof(channel.i_name));
	ichan = ioctl(sock, SIOCG80211CHANNEL, (caddr_t)&channel);

	memset(&bssid, 0, sizeof(bssid));
	strlcpy(bssid.i_name, ifname, sizeof(bssid.i_name));
	ibssid = ioctl(sock, SIOCG80211BSSID, &bssid);

	memset(&wpa, 0, sizeof(wpa));
	strlcpy(wpa.i_name, ifname, sizeof(wpa.i_name));
	iwpa = ioctl(sock, SIOCG80211WPAPARMS, &wpa);

	/* check if any ieee80211 option is active */
	if (inwid == 0 || ijoin == 0 || inwkey == 0 || ipsk == 0 ||
	    ipwr == 0 || ichan == 0 || ibssid == 0 || iwpa == 0)
		fputs("\tieee80211:", stdout);
	else
		return;

	if (inwid == 0) {
		/* nwid.i_nwid is not NUL terminated. */
		len = nwid.i_len;
		if (len > IEEE80211_NWID_LEN)
			len = IEEE80211_NWID_LEN;
		if (ijoin == 0 && join.i_flags & IEEE80211_JOIN_FOUND)
			fputs(" join ", stdout);
		else
			fputs(" nwid ", stdout);
		print_string(nwid.i_nwid, len);
	}

	if (ichan == 0 && channel.i_channel != 0 &&
	    channel.i_channel != IEEE80211_CHAN_ANY)
		printf(" chan %u", channel.i_channel);

	memset(&zero_bssid, 0, sizeof(zero_bssid));
	if (ibssid == 0 &&
	    memcmp(bssid.i_bssid, zero_bssid, IEEE80211_ADDR_LEN) != 0) {
		memcpy(&ea.ether_addr_octet, bssid.i_bssid,
		    sizeof(ea.ether_addr_octet));
		printf(" bssid %s", ether_ntoa(&ea));

		bzero(&nr, sizeof(nr));
		bcopy(bssid.i_bssid, &nr.nr_macaddr, sizeof(nr.nr_macaddr));
		strlcpy(nr.nr_ifname, ifname, sizeof(nr.nr_ifname));
		if (ioctl(sock, SIOCG80211NODE, &nr) == 0) {
			if (nr.nr_max_rssi)
				printf(" %u%%", IEEE80211_NODEREQ_RSSI(&nr));
			else
				printf(" %ddBm", nr.nr_rssi);
			assocfail = nr.nr_assoc_fail;
		}
	}

	if (inwkey == 0 && nwkey.i_wepon > IEEE80211_NWKEY_OPEN)
		fputs(" nwkey", stdout);

	if (ipsk == 0 && psk.i_enabled)
		fputs(" wpakey", stdout);
	if (iwpa == 0 && wpa.i_enabled) {
		const char *sep;

		fputs(" wpaprotos ", stdout); sep = "";
		if (wpa.i_protos & IEEE80211_WPA_PROTO_WPA1) {
			fputs("wpa1", stdout);
			sep = ",";
		}
		if (wpa.i_protos & IEEE80211_WPA_PROTO_WPA2)
			printf("%swpa2", sep);

		fputs(" wpaakms ", stdout); sep = "";
		if (wpa.i_akms & IEEE80211_WPA_AKM_PSK) {
			fputs("psk", stdout);
			sep = ",";
		}
		if (wpa.i_akms & IEEE80211_WPA_AKM_8021X)
			printf("%s802.1x", sep);

		fputs(" wpaciphers ", stdout);
		print_cipherset(wpa.i_ciphers);

		fputs(" wpagroupcipher ", stdout);
		print_cipherset(wpa.i_groupcipher);
	}

	if (ipwr == 0 && power.i_enabled)
		printf(" powersave on (%dms sleep)", power.i_maxsleep);

	if (ioctl(sock, SIOCG80211FLAGS, (caddr_t)&ifr) == 0 &&
	    ifr.ifr_flags) {
		putchar(' ');
		printb_status(ifr.ifr_flags, IEEE80211_F_USERBITS);
	}

	if (assocfail) {
		putchar(' ');
		print_assoc_failures(assocfail);
	}
	putchar('\n');
	if (show_join)
		join_status();
	if (shownet80211chans)
		ieee80211_listchans();
	else if (shownet80211nodes)
		ieee80211_listnodes();
}

void
showjoin(const char *cmd, int val)
{
	show_join = 1;
	return;
}

void
join_status(void)
{
	struct ieee80211_joinreq_all ja;
	struct ieee80211_join *jn = NULL;
	struct ieee80211_wpaparams *wpa;
	int jsz = 100;
	int ojsz;
	int i;
	int r;
	int maxlen, len;

	bzero(&ja, sizeof(ja));
	jn = recallocarray(NULL, 0, jsz, sizeof(*jn));
	if (jn == NULL)
		err(1, "recallocarray");
	ojsz = jsz;
	while (1) {
		ja.ja_node = jn;
		ja.ja_size = jsz * sizeof(*jn);
		strlcpy(ja.ja_ifname, ifname, sizeof(ja.ja_ifname));

		if ((r = ioctl(sock, SIOCG80211JOINALL, &ja)) != 0) {
			if (errno == E2BIG) {
				jsz += 100;
				jn = recallocarray(jn, ojsz, jsz, sizeof(*jn));
				if (jn == NULL)
					err(1, "recallocarray");
				ojsz = jsz;
				continue;
			} else if (errno != ENOENT)
				warn("%s: SIOCG80211JOINALL", ja.ja_ifname);
			return;
		}
		break;
	}

	if (!ja.ja_nodes)
		return;

	maxlen = 0;
	for (i = 0; i < ja.ja_nodes; i++) {
		len = len_string(jn[i].i_nwid, jn[i].i_len);
		if (len > maxlen)
			maxlen = len;
	}

	for (i = 0; i < ja.ja_nodes; i++) {
		printf("\t      ");
		if (jn[i].i_len > IEEE80211_NWID_LEN)
			jn[i].i_len = IEEE80211_NWID_LEN;
		len = print_string(jn[i].i_nwid, jn[i].i_len);
		printf("%-*s", maxlen - len, "");
		if (jn[i].i_flags) {
			const char *sep;
			printf(" ");

			if (jn[i].i_flags & IEEE80211_JOIN_NWKEY)
				printf("nwkey");

			if (jn[i].i_flags & IEEE80211_JOIN_WPA) {
				wpa = &jn[i].i_wpaparams;

				printf("wpaprotos "); sep = "";
				if (wpa->i_protos & IEEE80211_WPA_PROTO_WPA1) {
					printf("wpa1");
					sep = ",";
				}
				if (wpa->i_protos & IEEE80211_WPA_PROTO_WPA2)
					printf("%swpa2", sep);

				printf(" wpaakms "); sep = "";
				if (wpa->i_akms & IEEE80211_WPA_AKM_PSK) {
					printf("psk");
					sep = ",";
				}
				if (wpa->i_akms & IEEE80211_WPA_AKM_8021X)
					printf("%s802.1x", sep);

				printf(" wpaciphers ");
				print_cipherset(wpa->i_ciphers);

				printf(" wpagroupcipher ");
				print_cipherset(wpa->i_groupcipher);
			}
		}
		putchar('\n');
	}
}

void
ieee80211_listchans(void)
{
	static struct ieee80211_chaninfo chans[256];
	struct ieee80211_chanreq_all ca;
	int i;

	bzero(&ca, sizeof(ca));
	bzero(chans, sizeof(chans));
	ca.i_chans = chans;
	strlcpy(ca.i_name, ifname, sizeof(ca.i_name));

	if (ioctl(sock, SIOCG80211ALLCHANS, &ca) != 0) {
		warn("%s: SIOCG80211ALLCHANS", ca.i_name);
		return;
	}
	printf("\t\t%4s  %-8s  %s\n", "chan", "freq", "properties");
	for (i = 1; i < nitems(chans); i++) {
		if (chans[i].ic_freq == 0)
			continue;
		printf("\t\t%4d  %4d MHz  ", i, chans[i].ic_freq);
		if (chans[i].ic_flags & IEEE80211_CHANINFO_PASSIVE)
			printf("passive scan");
		else
			putchar('-');
		putchar('\n');
	}
}

/*
 * Returns an integer less than, equal to, or greater than zero if nr1's
 * RSSI is respectively greater than, equal to, or less than nr2's RSSI.
 */
static int
rssicmp(const void *nr1, const void *nr2)
{
	const struct ieee80211_nodereq *x = nr1, *y = nr2;
	return y->nr_rssi < x->nr_rssi ? -1 : y->nr_rssi > x->nr_rssi;
}

void
ieee80211_listnodes(void)
{
	struct ieee80211_nodereq_all na;
	struct ieee80211_nodereq nr[512];
	struct ifreq ifr;
	int i;

	if ((flags & IFF_UP) == 0) {
		printf("\t\tcannot scan, interface is down\n");
		return;
	}

	bzero(&ifr, sizeof(ifr));
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));

	if (ioctl(sock, SIOCS80211SCAN, (caddr_t)&ifr) != 0) {
		if (errno == EPERM)
			printf("\t\tno permission to scan\n");
		return;
	}

	bzero(&na, sizeof(na));
	bzero(&nr, sizeof(nr));
	na.na_node = nr;
	na.na_size = sizeof(nr);
	strlcpy(na.na_ifname, ifname, sizeof(na.na_ifname));

	if (ioctl(sock, SIOCG80211ALLNODES, &na) != 0) {
		warn("%s: SIOCG80211ALLNODES", na.na_ifname);
		return;
	}

	if (!na.na_nodes)
		printf("\t\tnone\n");
	else
		qsort(nr, na.na_nodes, sizeof(*nr), rssicmp);

	for (i = 0; i < na.na_nodes; i++) {
		printf("\t\t");
		ieee80211_printnode(&nr[i]);
		putchar('\n');
	}
}

void
ieee80211_printnode(struct ieee80211_nodereq *nr)
{
	int len, i;

	if (nr->nr_flags & IEEE80211_NODEREQ_AP ||
	    nr->nr_capinfo & IEEE80211_CAPINFO_IBSS) {
		len = nr->nr_nwid_len;
		if (len > IEEE80211_NWID_LEN)
			len = IEEE80211_NWID_LEN;
		printf("nwid ");
		print_string(nr->nr_nwid, len);
		putchar(' ');

		printf("chan %u ", nr->nr_channel);

		printf("bssid %s ",
		    ether_ntoa((struct ether_addr*)nr->nr_bssid));
	}

	if ((nr->nr_flags & IEEE80211_NODEREQ_AP) == 0)
		printf("lladdr %s ",
		    ether_ntoa((struct ether_addr*)nr->nr_macaddr));

	if (nr->nr_max_rssi)
		printf("%u%% ", IEEE80211_NODEREQ_RSSI(nr));
	else
		printf("%ddBm ", nr->nr_rssi);

	if (nr->nr_pwrsave)
		printf("powersave ");
	/*
	 * Print our current Tx rate for associated nodes.
	 * Print the fastest supported rate for APs.
	 */
	if ((nr->nr_flags & (IEEE80211_NODEREQ_AP)) == 0) {
		if (nr->nr_flags & IEEE80211_NODEREQ_VHT) {
			printf("VHT-MCS%d/%dSS", nr->nr_txmcs, nr->nr_vht_ss);
		} else if (nr->nr_flags & IEEE80211_NODEREQ_HT) {
			printf("HT-MCS%d ", nr->nr_txmcs);
		} else if (nr->nr_nrates) {
			printf("%uM ",
			    (nr->nr_rates[nr->nr_txrate] & IEEE80211_RATE_VAL)
			    / 2);
		}
	} else if (nr->nr_max_rxrate) {
		printf("%uM HT ", nr->nr_max_rxrate);
	} else if (nr->nr_rxmcs[0] != 0) {
		for (i = IEEE80211_HT_NUM_MCS - 1; i >= 0; i--) {
			if (nr->nr_rxmcs[i / 8] & (1 << (i / 10)))
				break;
		}
		printf("HT-MCS%d ", i);
	} else if (nr->nr_nrates) {
		printf("%uM ",
		    (nr->nr_rates[nr->nr_nrates - 1] & IEEE80211_RATE_VAL) / 2);
	}
	/* ESS is the default, skip it */
	nr->nr_capinfo &= ~IEEE80211_CAPINFO_ESS;
	if (nr->nr_capinfo) {
		printb_status(nr->nr_capinfo, IEEE80211_CAPINFO_BITS);
		if (nr->nr_capinfo & IEEE80211_CAPINFO_PRIVACY) {
			if (nr->nr_rsnprotos)
				print_rsnprotocol(nr->nr_rsnprotos,
				    nr->nr_rsnakms);
			else
				fputs(",wep", stdout);

			if (nr->nr_rsnakms & IEEE80211_WPA_AKM_8021X ||
			    nr->nr_rsnakms & IEEE80211_WPA_AKM_SHA256_8021X)
				fputs(",802.1x", stdout);
		}
		putchar(' ');
	}

	if ((nr->nr_flags & IEEE80211_NODEREQ_AP) == 0)
		printb_status(IEEE80211_NODEREQ_STATE(nr->nr_state),
		    IEEE80211_NODEREQ_STATE_BITS);
	else if (nr->nr_assoc_fail)
		print_assoc_failures(nr->nr_assoc_fail);
}

void
init_current_media(void)
{
	struct ifmediareq ifmr;

	/*
	 * If we have not yet done so, grab the currently-selected
	 * media.
	 */
	if ((actions & (A_MEDIA|A_MEDIAOPT|A_MEDIAMODE)) == 0) {
		(void) memset(&ifmr, 0, sizeof(ifmr));
		(void) strlcpy(ifmr.ifm_name, ifname, sizeof(ifmr.ifm_name));

		if (ioctl(sock, SIOCGIFMEDIA, (caddr_t)&ifmr) == -1) {
			/*
			 * If we get E2BIG, the kernel is telling us
			 * that there are more, so we can ignore it.
			 */
			if (errno != E2BIG)
				err(1, "%s: SIOCGIFMEDIA", ifmr.ifm_name);
		}

		media_current = ifmr.ifm_current;
	}

	/* Sanity. */
	if (IFM_TYPE(media_current) == 0)
		errx(1, "%s: no link type?", ifname);
}

void
process_media_commands(void)
{

	if ((actions & (A_MEDIA|A_MEDIAOPT|A_MEDIAMODE)) == 0) {
		/* Nothing to do. */
		return;
	}

	/*
	 * Media already set up, and commands sanity-checked.  Set/clear
	 * any options, and we're ready to go.
	 */
	media_current |= mediaopt_set;
	media_current &= ~mediaopt_clear;

	(void) strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	ifr.ifr_media = media_current;

	if (ioctl(sock, SIOCSIFMEDIA, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCSIFMEDIA", ifr.ifr_name);
}

void
setmedia(const char *val, int d)
{
	uint64_t type, subtype, inst;

	if (val == NULL) {
		if (showmediaflag)
			usage();
		showmediaflag = 1;
		return;
	}

	init_current_media();

	/* Only one media command may be given. */
	if (actions & A_MEDIA)
		errx(1, "only one `media' command may be issued");

	/* Must not come after mode commands */
	if (actions & A_MEDIAMODE)
		errx(1, "may not issue `media' after `mode' commands");

	/* Must not come after mediaopt commands */
	if (actions & A_MEDIAOPT)
		errx(1, "may not issue `media' after `mediaopt' commands");

	/*
	 * No need to check if `instance' has been issued; setmediainst()
	 * craps out if `media' has not been specified.
	 */

	type = IFM_TYPE(media_current);
	inst = IFM_INST(media_current);

	/* Look up the subtype. */
	subtype = get_media_subtype(type, val);

	/* Build the new current media word. */
	media_current = IFM_MAKEWORD(type, subtype, 0, inst);

	/* Media will be set after other processing is complete. */
}

void
setmediamode(const char *val, int d)
{
	uint64_t type, subtype, options, inst, mode;

	init_current_media();

	/* Can only issue `mode' once. */
	if (actions & A_MEDIAMODE)
		errx(1, "only one `mode' command may be issued");

	type = IFM_TYPE(media_current);
	subtype = IFM_SUBTYPE(media_current);
	options = IFM_OPTIONS(media_current);
	inst = IFM_INST(media_current);

	if ((mode = get_media_mode(type, val)) == -1)
		errx(1, "invalid media mode: %s", val);
	media_current = IFM_MAKEWORD(type, subtype, options, inst) | mode;
	/* Media will be set after other processing is complete. */
}

void
unsetmediamode(const char *val, int d)
{
	uint64_t type, subtype, options, inst;

	init_current_media();

	/* Can only issue `mode' once. */
	if (actions & A_MEDIAMODE)
		errx(1, "only one `mode' command may be issued");

	type = IFM_TYPE(media_current);
	subtype = IFM_SUBTYPE(media_current);
	options = IFM_OPTIONS(media_current);
	inst = IFM_INST(media_current);

	media_current = IFM_MAKEWORD(type, subtype, options, inst) |
	    (IFM_AUTO << IFM_MSHIFT);
	/* Media will be set after other processing is complete. */
}

void
setmediaopt(const char *val, int d)
{

	init_current_media();

	/* Can only issue `mediaopt' once. */
	if (actions & A_MEDIAOPTSET)
		errx(1, "only one `mediaopt' command may be issued");

	/* Can't issue `mediaopt' if `instance' has already been issued. */
	if (actions & A_MEDIAINST)
		errx(1, "may not issue `mediaopt' after `instance'");

	mediaopt_set = get_media_options(IFM_TYPE(media_current), val);

	/* Media will be set after other processing is complete. */
}

void
unsetmediaopt(const char *val, int d)
{

	init_current_media();

	/* Can only issue `-mediaopt' once. */
	if (actions & A_MEDIAOPTCLR)
		errx(1, "only one `-mediaopt' command may be issued");

	/* May not issue `media' and `-mediaopt'. */
	if (actions & A_MEDIA)
		errx(1, "may not issue both `media' and `-mediaopt'");

	/*
	 * No need to check for A_MEDIAINST, since the test for A_MEDIA
	 * implicitly checks for A_MEDIAINST.
	 */

	mediaopt_clear = get_media_options(IFM_TYPE(media_current), val);

	/* Media will be set after other processing is complete. */
}

void
setmediainst(const char *val, int d)
{
	uint64_t type, subtype, options, inst;
	const char *errmsg = NULL;

	init_current_media();

	/* Can only issue `instance' once. */
	if (actions & A_MEDIAINST)
		errx(1, "only one `instance' command may be issued");

	/* Must have already specified `media' */
	if ((actions & A_MEDIA) == 0)
		errx(1, "must specify `media' before `instance'");

	type = IFM_TYPE(media_current);
	subtype = IFM_SUBTYPE(media_current);
	options = IFM_OPTIONS(media_current);

	inst = strtonum(val, 0, IFM_INST_MAX, &errmsg);
	if (errmsg)
		errx(1, "media instance %s: %s", val, errmsg);

	media_current = IFM_MAKEWORD(type, subtype, options, inst);

	/* Media will be set after other processing is complete. */
}


const struct ifmedia_description ifm_type_descriptions[] =
    IFM_TYPE_DESCRIPTIONS;

const struct ifmedia_description ifm_subtype_descriptions[] =
    IFM_SUBTYPE_DESCRIPTIONS;

const struct ifmedia_description ifm_mode_descriptions[] =
    IFM_MODE_DESCRIPTIONS;

const struct ifmedia_description ifm_option_descriptions[] =
    IFM_OPTION_DESCRIPTIONS;

const char *
get_media_type_string(uint64_t mword)
{
	const struct ifmedia_description *desc;

	for (desc = ifm_type_descriptions; desc->ifmt_string != NULL;
	    desc++) {
		if (IFM_TYPE(mword) == desc->ifmt_word)
			return (desc->ifmt_string);
	}
	return ("<unknown type>");
}

const char *
get_media_subtype_string(uint64_t mword)
{
	const struct ifmedia_description *desc;

	for (desc = ifm_subtype_descriptions; desc->ifmt_string != NULL;
	    desc++) {
		if (IFM_TYPE_MATCH(desc->ifmt_word, mword) &&
		    IFM_SUBTYPE(desc->ifmt_word) == IFM_SUBTYPE(mword))
			return (desc->ifmt_string);
	}
	return ("<unknown subtype>");
}

uint64_t
get_media_subtype(uint64_t type, const char *val)
{
	uint64_t rval;

	rval = lookup_media_word(ifm_subtype_descriptions, type, val);
	if (rval == -1)
		errx(1, "unknown %s media subtype: %s",
		    get_media_type_string(type), val);

	return (rval);
}

uint64_t
get_media_mode(uint64_t type, const char *val)
{
	uint64_t rval;

	rval = lookup_media_word(ifm_mode_descriptions, type, val);
	if (rval == -1)
		errx(1, "unknown %s media mode: %s",
		    get_media_type_string(type), val);
	return (rval);
}

uint64_t
get_media_options(uint64_t type, const char *val)
{
	char *optlist, *str;
	uint64_t option, rval = 0;

	/* We muck with the string, so copy it. */
	optlist = strdup(val);
	if (optlist == NULL)
		err(1, "strdup");
	str = optlist;

	/*
	 * Look up the options in the user-provided comma-separated list.
	 */
	for (; (str = strtok(str, ",")) != NULL; str = NULL) {
		option = lookup_media_word(ifm_option_descriptions, type, str);
		if (option == -1)
			errx(1, "unknown %s media option: %s",
			    get_media_type_string(type), str);
		rval |= IFM_OPTIONS(option);
	}

	free(optlist);
	return (rval);
}

uint64_t
lookup_media_word(const struct ifmedia_description *desc, uint64_t type,
    const char *val)
{

	for (; desc->ifmt_string != NULL; desc++) {
		if (IFM_TYPE_MATCH(desc->ifmt_word, type) &&
		    strcasecmp(desc->ifmt_string, val) == 0)
			return (desc->ifmt_word);
	}
	return (-1);
}

void
print_media_word(uint64_t ifmw, int print_type, int as_syntax)
{
	const struct ifmedia_description *desc;
	uint64_t seen_option = 0;

	if (print_type)
		printf("%s ", get_media_type_string(ifmw));
	printf("%s%s", as_syntax ? "media " : "",
	    get_media_subtype_string(ifmw));

	/* Find mode. */
	if (IFM_MODE(ifmw) != 0) {
		for (desc = ifm_mode_descriptions; desc->ifmt_string != NULL;
		    desc++) {
			if (IFM_TYPE_MATCH(desc->ifmt_word, ifmw) &&
			    IFM_MODE(ifmw) == IFM_MODE(desc->ifmt_word)) {
				printf(" mode %s", desc->ifmt_string);
				break;
			}
		}
	}

	/* Find options. */
	for (desc = ifm_option_descriptions; desc->ifmt_string != NULL;
	    desc++) {
		if (IFM_TYPE_MATCH(desc->ifmt_word, ifmw) &&
		    (IFM_OPTIONS(ifmw) & IFM_OPTIONS(desc->ifmt_word)) != 0 &&
		    (seen_option & IFM_OPTIONS(desc->ifmt_word)) == 0) {
			if (seen_option == 0)
				printf(" %s", as_syntax ? "mediaopt " : "");
			printf("%s%s", seen_option ? "," : "",
			    desc->ifmt_string);
			seen_option |= IFM_OPTIONS(desc->ifmt_word);
		}
	}
	if (IFM_INST(ifmw) != 0)
		printf(" instance %lld", IFM_INST(ifmw));
}

static void
print_tunnel(const struct if_laddrreq *req)
{
	char psrcaddr[NI_MAXHOST];
	char psrcport[NI_MAXSERV];
	char pdstaddr[NI_MAXHOST];
	char pdstport[NI_MAXSERV];
	const char *ver = "";
	const int niflag = NI_NUMERICHOST | NI_NUMERICSERV | NI_DGRAM;

	if (req == NULL) {
		printf("(unset)");
		return;
	}

	psrcaddr[0] = pdstaddr[0] = '\0';

	if (getnameinfo((struct sockaddr *)&req->addr, req->addr.ss_len,
	    psrcaddr, sizeof(psrcaddr), psrcport, sizeof(psrcport),
	    niflag) != 0)
		strlcpy(psrcaddr, "<error>", sizeof(psrcaddr));
	if (req->addr.ss_family == AF_INET6)
		ver = "6";

	printf("inet%s %s", ver, psrcaddr);
	if (strcmp(psrcport, "0") != 0)
		printf(":%s", psrcport);

	if (req->dstaddr.ss_family != AF_UNSPEC) {
		if (getnameinfo((struct sockaddr *)&req->dstaddr,
		    req->dstaddr.ss_len, pdstaddr, sizeof(pdstaddr),
		    pdstport, sizeof(pdstport), niflag) != 0)
			strlcpy(pdstaddr, "<error>", sizeof(pdstaddr));

		printf(" --> %s", pdstaddr);
		if (strcmp(pdstport, "0") != 0)
			printf(":%s", pdstport);
	}
}

static void
phys_status(int force)
{
	struct if_laddrreq req;
	struct if_laddrreq *r = &req;

	memset(&req, 0, sizeof(req));
	(void) strlcpy(req.iflr_name, ifname, sizeof(req.iflr_name));
	if (ioctl(sock, SIOCGLIFPHYADDR, (caddr_t)&req) == -1) {
		if (errno != EADDRNOTAVAIL)
			return;

		r = NULL;
	}

	printf("\ttunnel: ");
	print_tunnel(r);

	if (ioctl(sock, SIOCGLIFPHYTTL, (caddr_t)&ifr) == 0) {
		if (ifr.ifr_ttl == -1)
			printf(" ttl copy");
		else if (ifr.ifr_ttl > 0)
			printf(" ttl %d", ifr.ifr_ttl);
	}

	if (ioctl(sock, SIOCGLIFPHYDF, (caddr_t)&ifr) == 0)
		printf(" %s", ifr.ifr_df ? "df" : "nodf");

#ifndef SMALL
	if (ioctl(sock, SIOCGLIFPHYECN, (caddr_t)&ifr) == 0)
		printf(" %s", ifr.ifr_metric ? "ecn" : "noecn");

	if (ioctl(sock, SIOCGLIFPHYRTABLE, (caddr_t)&ifr) == 0 &&
	    (rdomainid != 0 || ifr.ifr_rdomainid != 0))
		printf(" rdomain %d", ifr.ifr_rdomainid);
#endif
	printf("\n");
}

#ifndef SMALL
const uint64_t ifm_status_valid_list[] = IFM_STATUS_VALID_LIST;

const struct ifmedia_status_description ifm_status_descriptions[] =
	IFM_STATUS_DESCRIPTIONS;
#endif

const struct if_status_description if_status_descriptions[] =
	LINK_STATE_DESCRIPTIONS;

const char *
get_linkstate(int mt, int link_state)
{
	const struct if_status_description *p;
	static char buf[8];

	for (p = if_status_descriptions; p->ifs_string != NULL; p++) {
		if (LINK_STATE_DESC_MATCH(p, mt, link_state))
			return (p->ifs_string);
	}
	snprintf(buf, sizeof(buf), "[#%d]", link_state);
	return buf;
}

/*
 * Print the status of the interface.  If an address family was
 * specified, show it and it only; otherwise, show them all.
 */
void
status(int link, struct sockaddr_dl *sdl, int ls, int ifaliases)
{
	const struct afswtch *p = afp;
	struct ifmediareq ifmr;
#ifndef SMALL
	struct ifreq ifrdesc;
	struct ifkalivereq ikardesc;
	char ifdescr[IFDESCRSIZE];
	char pifname[IF_NAMESIZE];
#endif
	uint64_t *media_list;
	int i;
	char sep;


	printf("%s: ", ifname);
	printb("flags", flags | (xflags << 16), IFFBITS);
#ifndef SMALL
	if (rdomainid)
		printf(" rdomain %d", rdomainid);
#endif
	if (metric)
		printf(" metric %lu", metric);
	if (mtu)
		printf(" mtu %lu", mtu);
	putchar('\n');
#ifndef SMALL
	if (showcapsflag)
		printifhwfeatures(NULL, 1);
#endif
	if (sdl != NULL && sdl->sdl_alen &&
	    (sdl->sdl_type == IFT_ETHER || sdl->sdl_type == IFT_CARP))
		(void)printf("\tlladdr %s\n", ether_ntoa(
		    (struct ether_addr *)LLADDR(sdl)));

	sep = '\t';
#ifndef SMALL
	(void) memset(&ifrdesc, 0, sizeof(ifrdesc));
	(void) strlcpy(ifrdesc.ifr_name, ifname, sizeof(ifrdesc.ifr_name));
	ifrdesc.ifr_data = (caddr_t)&ifdescr;
	if (ioctl(sock, SIOCGIFDESCR, &ifrdesc) == 0 &&
	    strlen(ifrdesc.ifr_data))
		printf("\tdescription: %s\n", ifrdesc.ifr_data);

	if (sdl != NULL) {
		printf("%cindex %u", sep, sdl->sdl_index);
		sep = ' ';
	}
	if (!is_bridge() && ioctl(sock, SIOCGIFPRIORITY, &ifrdesc) == 0) {
		printf("%cpriority %d", sep, ifrdesc.ifr_metric);
		sep = ' ';
	}
#endif
	printf("%cllprio %d\n", sep, llprio);

#ifndef SMALL
	(void) memset(&ikardesc, 0, sizeof(ikardesc));
	(void) strlcpy(ikardesc.ikar_name, ifname, sizeof(ikardesc.ikar_name));
	if (ioctl(sock, SIOCGETKALIVE, &ikardesc) == 0 &&
	    (ikardesc.ikar_timeo != 0 || ikardesc.ikar_cnt != 0))
		printf("\tkeepalive: timeout %d count %d\n",
		    ikardesc.ikar_timeo, ikardesc.ikar_cnt);
	if (ioctl(sock, SIOCGIFPAIR, &ifrdesc) == 0 && ifrdesc.ifr_index != 0 &&
	    if_indextoname(ifrdesc.ifr_index, pifname) != NULL)
		printf("\tpatch: %s\n", pifname);
#endif
	getencap();
#ifndef SMALL
	carp_status();
	pfsync_status();
	pppoe_status();
	sppp_status();
	mpls_status();
	pflow_status();
	umb_status();
	wg_status(ifaliases);
#endif
	trunk_status();
	getifgroups();

	(void) memset(&ifmr, 0, sizeof(ifmr));
	(void) strlcpy(ifmr.ifm_name, ifname, sizeof(ifmr.ifm_name));

	if (ioctl(sock, SIOCGIFMEDIA, (caddr_t)&ifmr) == -1) {
		/*
		 * Interface doesn't support SIOC{G,S}IFMEDIA.
		 */
		if (ls != LINK_STATE_UNKNOWN)
			printf("\tstatus: %s\n",
			    get_linkstate(sdl->sdl_type, ls));
		goto proto_status;
	}

	if (ifmr.ifm_count == 0) {
		warnx("%s: no media types?", ifname);
		goto proto_status;
	}

	media_list = calloc(ifmr.ifm_count, sizeof(*media_list));
	if (media_list == NULL)
		err(1, "calloc");
	ifmr.ifm_ulist = media_list;

	if (ioctl(sock, SIOCGIFMEDIA, (caddr_t)&ifmr) == -1)
		err(1, "%s: SIOCGIFMEDIA", ifmr.ifm_name);

	printf("\tmedia: ");
	print_media_word(ifmr.ifm_current, 1, 0);
	if (ifmr.ifm_active != ifmr.ifm_current) {
		putchar(' ');
		putchar('(');
		print_media_word(ifmr.ifm_active, 0, 0);
		putchar(')');
	}
	putchar('\n');

#ifdef SMALL
	printf("\tstatus: %s\n", get_linkstate(sdl->sdl_type, ls));
#else
	if (ifmr.ifm_status & IFM_AVALID) {
		const struct ifmedia_status_description *ifms;
		int bitno, found = 0;

		printf("\tstatus: ");
		for (bitno = 0; ifm_status_valid_list[bitno] != 0; bitno++) {
			for (ifms = ifm_status_descriptions;
			    ifms->ifms_valid != 0; ifms++) {
				if (ifms->ifms_type !=
				    IFM_TYPE(ifmr.ifm_current) ||
				    ifms->ifms_valid !=
				    ifm_status_valid_list[bitno])
					continue;
				printf("%s%s", found ? ", " : "",
				    IFM_STATUS_DESC(ifms, ifmr.ifm_status));
				found = 1;

				/*
				 * For each valid indicator bit, there's
				 * only one entry for each media type, so
				 * terminate the inner loop now.
				 */
				break;
			}
		}

		if (found == 0)
			printf("unknown");
		putchar('\n');
	}

	if (showtransceiver) {
		if (if_sff_info(0) == -1)
			if (!aflag && errno != EPERM && errno != ENOTTY)
				warn("%s transceiver", ifname);
	}
#endif
	ieee80211_status();

	if (showmediaflag) {
		uint64_t type;
		int printed_type = 0;

		for (type = IFM_NMIN; type <= IFM_NMAX; type += IFM_NMIN) {
			for (i = 0, printed_type = 0; i < ifmr.ifm_count; i++) {
				if (IFM_TYPE(media_list[i]) == type) {

					/*
					 * Don't advertise media with fixed
					 * data rates for wireless interfaces.
					 * Normal people don't need these.
					 */
					if (type == IFM_IEEE80211 &&
					    (media_list[i] & IFM_TMASK) !=
					    IFM_AUTO)
						continue;

					if (printed_type == 0) {
					    printf("\tsupported media:\n");
					    printed_type = 1;
					}
					printf("\t\t");
					print_media_word(media_list[i], 0, 1);
					printf("\n");
				}
			}
		}
	}

	free(media_list);

 proto_status:
	if (link == 0) {
		if ((p = afp) != NULL) {
			p->af_status(1);
		} else for (p = afs; p->af_name; p++) {
			ifr.ifr_addr.sa_family = p->af_af;
			p->af_status(0);
		}
	}

	phys_status(0);
#ifndef SMALL
	bridge_status();
#endif
}

void
in_status(int force)
{
	struct sockaddr_in *sin, sin2;

	getsock(AF_INET);
	if (sock == -1) {
		if (errno == EPROTONOSUPPORT)
			return;
		err(1, "socket");
	}
	(void) strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	sin = (struct sockaddr_in *)&ifr.ifr_addr;

	/*
	 * We keep the interface address and reset it before each
	 * ioctl() so we can get ifaliases information (as opposed
	 * to the primary interface netmask/dstaddr/broadaddr, if
	 * the ifr_addr field is zero).
	 */
	memcpy(&sin2, &ifr.ifr_addr, sizeof(sin2));

	printf("\tinet %s", inet_ntoa(sin->sin_addr));
	(void) strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	if (ioctl(sock, SIOCGIFNETMASK, (caddr_t)&ifr) == -1) {
		if (errno != EADDRNOTAVAIL)
			warn("SIOCGIFNETMASK");
		memset(&ifr.ifr_addr, 0, sizeof(ifr.ifr_addr));
	} else
		netmask.sin_addr =
		    ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr;
	if (flags & IFF_POINTOPOINT) {
		memcpy(&ifr.ifr_addr, &sin2, sizeof(sin2));
		if (ioctl(sock, SIOCGIFDSTADDR, (caddr_t)&ifr) == -1) {
			if (errno == EADDRNOTAVAIL)
			    memset(&ifr.ifr_addr, 0, sizeof(ifr.ifr_addr));
			else
			    warn("SIOCGIFDSTADDR");
		}
		(void) strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
		sin = (struct sockaddr_in *)&ifr.ifr_dstaddr;
		printf(" --> %s", inet_ntoa(sin->sin_addr));
	}
	printf(" netmask 0x%x", ntohl(netmask.sin_addr.s_addr));
	if (flags & IFF_BROADCAST) {
		memcpy(&ifr.ifr_addr, &sin2, sizeof(sin2));
		if (ioctl(sock, SIOCGIFBRDADDR, (caddr_t)&ifr) == -1) {
			if (errno == EADDRNOTAVAIL)
			    memset(&ifr.ifr_addr, 0, sizeof(ifr.ifr_addr));
			else
			    warn("SIOCGIFBRDADDR");
		}
		(void) strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
		sin = (struct sockaddr_in *)&ifr.ifr_addr;
		if (sin->sin_addr.s_addr != 0)
			printf(" broadcast %s", inet_ntoa(sin->sin_addr));
	}
	putchar('\n');
}

void
setifprefixlen(const char *addr, int d)
{
	if (afp->af_getprefix)
		afp->af_getprefix(addr, MASK);
	explicit_prefix = 1;
}

void
in6_fillscopeid(struct sockaddr_in6 *sin6)
{
#ifdef __KAME__
	if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr) &&
	    sin6->sin6_scope_id == 0) {
		sin6->sin6_scope_id =
			ntohs(*(u_int16_t *)&sin6->sin6_addr.s6_addr[2]);
		sin6->sin6_addr.s6_addr[2] = sin6->sin6_addr.s6_addr[3] = 0;
	}
#endif /* __KAME__ */
}

/* XXX not really an alias */
void
in6_alias(struct in6_ifreq *creq)
{
	struct sockaddr_in6 *sin6;
	struct	in6_ifreq ifr6;		/* shadows file static variable */
	u_int32_t scopeid;
	char hbuf[NI_MAXHOST];
	const int niflag = NI_NUMERICHOST;

	/* Get the non-alias address for this interface. */
	getsock(AF_INET6);
	if (sock == -1) {
		if (errno == EPROTONOSUPPORT)
			return;
		err(1, "socket");
	}

	sin6 = (struct sockaddr_in6 *)&creq->ifr_addr;

	in6_fillscopeid(sin6);
	scopeid = sin6->sin6_scope_id;
	if (getnameinfo((struct sockaddr *)sin6, sin6->sin6_len,
	    hbuf, sizeof(hbuf), NULL, 0, niflag) != 0)
		strlcpy(hbuf, "", sizeof hbuf);
	printf("\tinet6 %s", hbuf);

	if (flags & IFF_POINTOPOINT) {
		(void) memset(&ifr6, 0, sizeof(ifr6));
		(void) strlcpy(ifr6.ifr_name, ifname, sizeof(ifr6.ifr_name));
		ifr6.ifr_addr = creq->ifr_addr;
		if (ioctl(sock, SIOCGIFDSTADDR_IN6, (caddr_t)&ifr6) == -1) {
			if (errno != EADDRNOTAVAIL)
				warn("SIOCGIFDSTADDR_IN6");
			(void) memset(&ifr6.ifr_addr, 0, sizeof(ifr6.ifr_addr));
			ifr6.ifr_addr.sin6_family = AF_INET6;
			ifr6.ifr_addr.sin6_len = sizeof(struct sockaddr_in6);
		}
		sin6 = (struct sockaddr_in6 *)&ifr6.ifr_addr;
		in6_fillscopeid(sin6);
		if (getnameinfo((struct sockaddr *)sin6, sin6->sin6_len,
		    hbuf, sizeof(hbuf), NULL, 0, niflag) != 0)
			strlcpy(hbuf, "", sizeof hbuf);
		printf(" --> %s", hbuf);
	}

	(void) memset(&ifr6, 0, sizeof(ifr6));
	(void) strlcpy(ifr6.ifr_name, ifname, sizeof(ifr6.ifr_name));
	ifr6.ifr_addr = creq->ifr_addr;
	if (ioctl(sock, SIOCGIFNETMASK_IN6, (caddr_t)&ifr6) == -1) {
		if (errno != EADDRNOTAVAIL)
			warn("SIOCGIFNETMASK_IN6");
	} else {
		sin6 = (struct sockaddr_in6 *)&ifr6.ifr_addr;
		printf(" prefixlen %d", prefix(&sin6->sin6_addr,
		    sizeof(struct in6_addr)));
	}

	(void) memset(&ifr6, 0, sizeof(ifr6));
	(void) strlcpy(ifr6.ifr_name, ifname, sizeof(ifr6.ifr_name));
	ifr6.ifr_addr = creq->ifr_addr;
	if (ioctl(sock, SIOCGIFAFLAG_IN6, (caddr_t)&ifr6) == -1) {
		if (errno != EADDRNOTAVAIL)
			warn("SIOCGIFAFLAG_IN6");
	} else {
		if (ifr6.ifr_ifru.ifru_flags6 & IN6_IFF_ANYCAST)
			printf(" anycast");
		if (ifr6.ifr_ifru.ifru_flags6 & IN6_IFF_TENTATIVE)
			printf(" tentative");
		if (ifr6.ifr_ifru.ifru_flags6 & IN6_IFF_DUPLICATED)
			printf(" duplicated");
		if (ifr6.ifr_ifru.ifru_flags6 & IN6_IFF_DETACHED)
			printf(" detached");
		if (ifr6.ifr_ifru.ifru_flags6 & IN6_IFF_DEPRECATED)
			printf(" deprecated");
		if (ifr6.ifr_ifru.ifru_flags6 & IN6_IFF_AUTOCONF)
			printf(" autoconf");
		if (ifr6.ifr_ifru.ifru_flags6 & IN6_IFF_TEMPORARY)
			printf(" temporary");
	}

	if (scopeid)
		printf(" scopeid 0x%x", scopeid);

	if (Lflag) {
		struct in6_addrlifetime *lifetime;

		(void) memset(&ifr6, 0, sizeof(ifr6));
		(void) strlcpy(ifr6.ifr_name, ifname, sizeof(ifr6.ifr_name));
		ifr6.ifr_addr = creq->ifr_addr;
		lifetime = &ifr6.ifr_ifru.ifru_lifetime;
		if (ioctl(sock, SIOCGIFALIFETIME_IN6, (caddr_t)&ifr6) == -1) {
			if (errno != EADDRNOTAVAIL)
				warn("SIOCGIFALIFETIME_IN6");
		} else if (lifetime->ia6t_preferred || lifetime->ia6t_expire) {
			time_t t = time(NULL);

			printf(" pltime ");
			if (lifetime->ia6t_preferred) {
				printf("%s", lifetime->ia6t_preferred < t
				    ? "0" :
				    sec2str(lifetime->ia6t_preferred - t));
			} else
				printf("infty");

			printf(" vltime ");
			if (lifetime->ia6t_expire) {
				printf("%s", lifetime->ia6t_expire < t
				    ? "0"
				    : sec2str(lifetime->ia6t_expire - t));
			} else
				printf("infty");
		}
	}

	printf("\n");
}

void
in6_status(int force)
{
	in6_alias((struct in6_ifreq *)&ifr6);
}

#ifndef SMALL
void
settunnel(const char *src, const char *dst)
{
	char srcbuf[HOST_NAME_MAX], dstbuf[HOST_NAME_MAX];
	const char *srcport, *dstport;
	const char *srcaddr, *dstaddr;
	struct addrinfo *srcres, *dstres;
	struct addrinfo hints = {
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_DGRAM,
		.ai_protocol = IPPROTO_UDP,
		.ai_flags = AI_PASSIVE,
	};
	int ecode;
	size_t len;
	struct if_laddrreq req;

	srcport = strchr(src, ':');
	if (srcport == NULL || srcport != strrchr(src, ':')) {
		/* no port or IPv6 */
		srcaddr = src;
		srcport = NULL;
	} else {
		len = srcport - src;
		if (len >= sizeof(srcbuf))
			errx(1, "src %s bad value", src);
		memcpy(srcbuf, src, len);
		srcbuf[len] = '\0';

		srcaddr = srcbuf;
		srcport++;
	}

	dstport = strchr(dst, ':');
	if (dstport == NULL || dstport != strrchr(dst, ':')) {
		/* no port or IPv6 */
		dstaddr = dst;
		dstport = NULL;
	} else {
		len = dstport - dst;
		if (len >= sizeof(dstbuf))
			errx(1, "dst %s bad value", dst);
		memcpy(dstbuf, dst, len);
		dstbuf[len] = '\0';

		dstaddr = dstbuf;
		dstport++;
	}

	if ((ecode = getaddrinfo(srcaddr, srcport, &hints, &srcres)) != 0)
		errx(1, "error in parsing address string: %s",
		    gai_strerror(ecode));

	hints.ai_flags = 0;
	if ((ecode = getaddrinfo(dstaddr, dstport, &hints, &dstres)) != 0)
		errx(1, "error in parsing address string: %s",
		    gai_strerror(ecode));

	if (srcres->ai_addr->sa_family != dstres->ai_addr->sa_family)
		errx(1,
		    "source and destination address families do not match");

	memset(&req, 0, sizeof(req));
	(void) strlcpy(req.iflr_name, ifname, sizeof(req.iflr_name));
	memcpy(&req.addr, srcres->ai_addr, srcres->ai_addrlen);
	memcpy(&req.dstaddr, dstres->ai_addr, dstres->ai_addrlen);
	if (ioctl(sock, SIOCSLIFPHYADDR, &req) == -1)
		warn("SIOCSLIFPHYADDR");

	freeaddrinfo(srcres);
	freeaddrinfo(dstres);
}

void
settunneladdr(const char *src, int ignored)
{
	char srcbuf[HOST_NAME_MAX];
	const char *srcport;
	const char *srcaddr;
	struct addrinfo *srcres;
	struct addrinfo hints = {
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_DGRAM,
		.ai_protocol = IPPROTO_UDP,
		.ai_flags = AI_PASSIVE,
	};
	struct if_laddrreq req;
	ssize_t len;
	int rv;

	srcport = strchr(src, ':');
	if (srcport == NULL || srcport != strrchr(src, ':')) {
		/* no port or IPv6 */
		srcaddr = src;
		srcport = NULL;
	} else {
		len = srcport - src;
		if (len >= sizeof(srcbuf))
			errx(1, "src %s bad value", src);
		memcpy(srcbuf, src, len);
		srcbuf[len] = '\0';

		srcaddr = srcbuf;
		srcport++;
	}

	rv = getaddrinfo(srcaddr, srcport, &hints, &srcres);
	if (rv != 0)
		errx(1, "tunneladdr %s: %s", src, gai_strerror(rv));

	memset(&req, 0, sizeof(req));
	len = strlcpy(req.iflr_name, ifname, sizeof(req.iflr_name));
	if (len >= sizeof(req.iflr_name))
		errx(1, "%s: Interface name too long", ifname);

	memcpy(&req.addr, srcres->ai_addr, srcres->ai_addrlen);

	req.dstaddr.ss_len = 2;
	req.dstaddr.ss_family = AF_UNSPEC;

	if (ioctl(sock, SIOCSLIFPHYADDR, &req) == -1)
		warn("tunneladdr %s", src);

	freeaddrinfo(srcres);
}

void
deletetunnel(const char *ignored, int alsoignored)
{
	if (ioctl(sock, SIOCDIFPHYADDR, &ifr) == -1)
		warn("SIOCDIFPHYADDR");
}

void
settunnelinst(const char *id, int param)
{
	const char *errmsg = NULL;
	int rdomainid;

	rdomainid = strtonum(id, 0, RT_TABLEID_MAX, &errmsg);
	if (errmsg)
		errx(1, "rdomain %s: %s", id, errmsg);

	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	ifr.ifr_rdomainid = rdomainid;
	if (ioctl(sock, SIOCSLIFPHYRTABLE, (caddr_t)&ifr) == -1)
		warn("SIOCSLIFPHYRTABLE");
}

void
unsettunnelinst(const char *ignored, int alsoignored)
{
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	ifr.ifr_rdomainid = 0;
	if (ioctl(sock, SIOCSLIFPHYRTABLE, (caddr_t)&ifr) == -1)
		warn("SIOCSLIFPHYRTABLE");
}

void
settunnelttl(const char *id, int param)
{
	const char *errmsg = NULL;
	int ttl;

	if (strcmp(id, "copy") == 0)
		ttl = -1;
	else {
		ttl = strtonum(id, 0, 0xff, &errmsg);
		if (errmsg)
			errx(1, "tunnelttl %s: %s", id, errmsg);
	}

	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	ifr.ifr_ttl = ttl;
	if (ioctl(sock, SIOCSLIFPHYTTL, (caddr_t)&ifr) == -1)
		warn("SIOCSLIFPHYTTL");
}

void
settunneldf(const char *ignored, int alsoignored)
{
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	ifr.ifr_df = 1;
	if (ioctl(sock, SIOCSLIFPHYDF, (caddr_t)&ifr) == -1)
		warn("SIOCSLIFPHYDF");
}

void
settunnelnodf(const char *ignored, int alsoignored)
{
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	ifr.ifr_df = 0;
	if (ioctl(sock, SIOCSLIFPHYDF, (caddr_t)&ifr) == -1)
		warn("SIOCSLIFPHYDF");
}

void
settunnelecn(const char *ignored, int alsoignored)
{
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	ifr.ifr_metric = 1;
	if (ioctl(sock, SIOCSLIFPHYECN, (caddr_t)&ifr) == -1)
		warn("SIOCSLIFPHYECN");
}

void
settunnelnoecn(const char *ignored, int alsoignored)
{
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	ifr.ifr_metric = 0;
	if (ioctl(sock, SIOCSLIFPHYECN, (caddr_t)&ifr) == -1)
		warn("SIOCSLIFPHYECN");
}

void
setvnetflowid(const char *ignored, int alsoignored)
{
	if (strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name)) >=
	    sizeof(ifr.ifr_name))
		errx(1, "vnetflowid: name is too long");

	ifr.ifr_vnetid = 1;
	if (ioctl(sock, SIOCSVNETFLOWID, &ifr) == -1)
		warn("SIOCSVNETFLOWID");
}

void
delvnetflowid(const char *ignored, int alsoignored)
{
	if (strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name)) >=
	    sizeof(ifr.ifr_name))
		errx(1, "vnetflowid: name is too long");

	ifr.ifr_vnetid = 0;
	if (ioctl(sock, SIOCSVNETFLOWID, &ifr) == -1)
		warn("SIOCSVNETFLOWID");
}

static void
pwe3_neighbor(void)
{
	const char *prefix = "pwe3 remote label";
	struct if_laddrreq req;
	char hbuf[NI_MAXHOST];
	struct sockaddr_mpls *smpls;
	int error;

	memset(&req, 0, sizeof(req));
	if (strlcpy(req.iflr_name, ifname, sizeof(req.iflr_name)) >=
	    sizeof(req.iflr_name))
		errx(1, "pwe3 neighbor: name is too long");

	if (ioctl(sock, SIOCGPWE3NEIGHBOR, &req) == -1) {
		if (errno != EADDRNOTAVAIL)
			return;

		printf(" %s (unset)", prefix);
		return;
	}

	if (req.dstaddr.ss_family != AF_MPLS) {
		warnc(EPFNOSUPPORT, "pwe3 neighbor");
		return;
	}
	smpls = (struct sockaddr_mpls *)&req.dstaddr;

	error = getnameinfo((struct sockaddr *)&req.addr, sizeof(req.addr),
	    hbuf, sizeof(hbuf), NULL, 0, NI_NUMERICHOST);
	if (error != 0) {
		warnx("%s: %s", prefix, gai_strerror(error));
		return;
	}

	printf(" %s %u on %s", prefix, smpls->smpls_label, hbuf);
}

static void
pwe3_cword(void)
{
	struct ifreq req;

	memset(&req, 0, sizeof(req));
	if (strlcpy(req.ifr_name, ifname, sizeof(req.ifr_name)) >=
	    sizeof(req.ifr_name))
		errx(1, "pwe3 control word: name is too long");

	if (ioctl(sock, SIOCGPWE3CTRLWORD, &req) == -1) {
		return;
	}

	printf(" %s", req.ifr_pwe3 ? "cw" : "nocw");
}

static void
pwe3_fword(void)
{
	struct ifreq req;

	memset(&req, 0, sizeof(req));
	if (strlcpy(req.ifr_name, ifname, sizeof(req.ifr_name)) >=
	    sizeof(req.ifr_name))
		errx(1, "pwe3 control word: name is too long");

	if (ioctl(sock, SIOCGPWE3FAT, &req) == -1)
		return;

	printf(" %s", req.ifr_pwe3 ? "fat" : "nofat");
}

void
mpls_status(void)
{
	struct shim_hdr	shim;

	bzero(&shim, sizeof(shim));
	ifr.ifr_data = (caddr_t)&shim;

	if (ioctl(sock, SIOCGETLABEL, (caddr_t)&ifr) == -1) {
		if (errno != EADDRNOTAVAIL)
			return;

		printf("\tmpls: label (unset)");
	} else
		printf("\tmpls: label %u", shim.shim_label);

	pwe3_neighbor();
	pwe3_cword();
	pwe3_fword();

	printf("\n");
}

void
setmplslabel(const char *val, int d)
{
	struct shim_hdr	 shim;
	const char	*estr;

	bzero(&shim, sizeof(shim));
	ifr.ifr_data = (caddr_t)&shim;
	shim.shim_label = strtonum(val, 0, MPLS_LABEL_MAX, &estr);

	if (estr)
		errx(1, "mpls label %s is %s", val, estr);
	if (ioctl(sock, SIOCSETLABEL, (caddr_t)&ifr) == -1)
		warn("SIOCSETLABEL");
}

void
unsetmplslabel(const char *val, int d)
{
	struct ifreq req;

	memset(&req, 0, sizeof(req));
	if (strlcpy(req.ifr_name, ifname, sizeof(req.ifr_name)) >=
	    sizeof(req.ifr_name))
		errx(1, "interface name is too long");

	if (ioctl(sock, SIOCDELLABEL, (caddr_t)&ifr) == -1)
		warn("-mplslabel");
}

static void
setpwe3(unsigned long cmd, const char *cmdname, int value)
{
	struct ifreq req;

	memset(&req, 0, sizeof(req));
	if (strlcpy(req.ifr_name, ifname, sizeof(req.ifr_name)) >=
	    sizeof(req.ifr_name))
		errx(1, "interface name is too long");

	req.ifr_pwe3 = value;

	if (ioctl(sock, cmd, &req) == -1)
		warn("%s", cmdname);
}

void
setpwe3cw(const char *val, int d)
{
	setpwe3(SIOCSPWE3CTRLWORD, "pwecw", 1);
}

void
unsetpwe3cw(const char *val, int d)
{
	setpwe3(SIOCSPWE3CTRLWORD, "-pwecw", 0);
}

void
setpwe3fat(const char *val, int d)
{
	setpwe3(SIOCSPWE3FAT, "pwefat", 1);
}

void
unsetpwe3fat(const char *val, int d)
{
	setpwe3(SIOCSPWE3FAT, "-pwefat", 0);
}

void
setpwe3neighbor(const char *label, const char *neighbor)
{
	struct if_laddrreq req;
	struct addrinfo hints, *res;
	struct sockaddr_mpls *smpls = (struct sockaddr_mpls *)&req.dstaddr;
	const char *errstr;
	int error;

	memset(&req, 0, sizeof(req));
	if (strlcpy(req.iflr_name, ifname, sizeof(req.iflr_name)) >=
	    sizeof(req.iflr_name))
		errx(1, "interface name is too long");

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	error = getaddrinfo(neighbor, NULL, &hints, &res);
	if (error != 0)
		errx(1, "pweneighbor %s: %s", neighbor, gai_strerror(error));

	smpls->smpls_len = sizeof(*smpls);
	smpls->smpls_family = AF_MPLS;
	smpls->smpls_label = strtonum(label,
	    (MPLS_LABEL_RESERVED_MAX + 1), MPLS_LABEL_MAX, &errstr);
	if (errstr != NULL)
		errx(1, "pweneighbor: invalid label: %s", errstr);


	if (res->ai_addrlen > sizeof(req.addr))
		errx(1, "pweneighbors: unexpected socklen");

	memcpy(&req.addr, res->ai_addr, res->ai_addrlen);

	freeaddrinfo(res);

	if (ioctl(sock, SIOCSPWE3NEIGHBOR, &req) == -1)
		warn("pweneighbor");
}

void
unsetpwe3neighbor(const char *val, int d)
{
	struct ifreq req;

	memset(&req, 0, sizeof(req));
	if (strlcpy(req.ifr_name, ifname, sizeof(req.ifr_name)) >=
	    sizeof(req.ifr_name))
		errx(1, "interface name is too long");

	if (ioctl(sock, SIOCDPWE3NEIGHBOR, &req) == -1)
		warn("-pweneighbor");
}

void
transceiver(const char *value, int d)
{
	showtransceiver = 1;
}

void
transceiverdump(const char *value, int d)
{
	if (if_sff_info(1) == -1)
		err(1, "%s transceiver", ifname);
}
#endif /* SMALL */

void
getvnetflowid(struct ifencap *ife)
{
	if (strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name)) >=
	    sizeof(ifr.ifr_name))
		errx(1, "vnetflowid: name is too long");

	if (ioctl(sock, SIOCGVNETFLOWID, &ifr) == -1)
		return;

	if (ifr.ifr_vnetid)
		ife->ife_flags |= IFE_VNETFLOWID;
}

void
setvnetid(const char *id, int param)
{
	const char *errmsg = NULL;
	int64_t vnetid;

	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));

	if (strcasecmp("any", id) == 0)
		vnetid = -1;
	else {
		vnetid = strtonum(id, 0, INT64_MAX, &errmsg);
		if (errmsg)
			errx(1, "vnetid %s: %s", id, errmsg);
	}

	ifr.ifr_vnetid = vnetid;
	if (ioctl(sock, SIOCSVNETID, (caddr_t)&ifr) == -1)
		warn("SIOCSVNETID");
}

void
delvnetid(const char *ignored, int alsoignored)
{
	if (ioctl(sock, SIOCDVNETID, &ifr) == -1)
		warn("SIOCDVNETID");
}

void
getvnetid(struct ifencap *ife)
{
	if (strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name)) >=
	    sizeof(ifr.ifr_name))
		errx(1, "vnetid: name is too long");

	if (ioctl(sock, SIOCGVNETID, &ifr) == -1) {
		if (errno != EADDRNOTAVAIL)
			return;

		ife->ife_flags |= IFE_VNETID_NONE;
		return;
	}

	if (ifr.ifr_vnetid < 0) {
		ife->ife_flags |= IFE_VNETID_ANY;
		return;
	}

	ife->ife_flags |= IFE_VNETID_SET;
	ife->ife_vnetid = ifr.ifr_vnetid;
}

void
setifparent(const char *id, int param)
{
	struct if_parent ifp;

	if (strlcpy(ifp.ifp_name, ifname, sizeof(ifp.ifp_name)) >=
	    sizeof(ifp.ifp_name))
		errx(1, "parent: name too long");

	if (strlcpy(ifp.ifp_parent, id, sizeof(ifp.ifp_parent)) >=
	    sizeof(ifp.ifp_parent))
		errx(1, "parent: parent too long");

	if (ioctl(sock, SIOCSIFPARENT, (caddr_t)&ifp) == -1)
		warn("%s: %s: SIOCSIFPARENT", ifp.ifp_name, ifp.ifp_parent);
}

void
delifparent(const char *ignored, int alsoignored)
{
	if (ioctl(sock, SIOCDIFPARENT, &ifr) == -1)
		warn("SIOCDIFPARENT");
}

void
getifparent(struct ifencap *ife)
{
	struct if_parent ifp;

	memset(&ifp, 0, sizeof(ifp));
	if (strlcpy(ifp.ifp_name, ifname, sizeof(ifp.ifp_name)) >=
	    sizeof(ifp.ifp_name))
		errx(1, "parent: name too long");

	if (ioctl(sock, SIOCGIFPARENT, (caddr_t)&ifp) == -1) {
		if (errno != EADDRNOTAVAIL)
			return;

		ife->ife_flags |= IFE_PARENT_NONE;
	} else {
		memcpy(ife->ife_parent, ifp.ifp_parent,
		    sizeof(ife->ife_parent));
		ife->ife_flags |= IFE_PARENT_SET;
	}
}

#ifndef SMALL
void
gettxprio(struct ifencap *ife)
{
	if (strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name)) >=
	    sizeof(ifr.ifr_name))
		errx(1, "hdr prio: name is too long");

	if (ioctl(sock, SIOCGTXHPRIO, (caddr_t)&ifr) == -1)
		return;

	ife->ife_flags |= IFE_TXHPRIO_SET;
	ife->ife_txhprio = ifr.ifr_hdrprio;
}

void
settxprio(const char *val, int d)
{
	const char *errmsg = NULL;

	if (strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name)) >=
	    sizeof(ifr.ifr_name))
		errx(1, "tx prio: name is too long");

	if (strcmp(val, "packet") == 0)
		ifr.ifr_hdrprio = IF_HDRPRIO_PACKET;
	else if (strcmp(val, "payload") == 0)
		ifr.ifr_hdrprio = IF_HDRPRIO_PAYLOAD;
	else {
		ifr.ifr_hdrprio = strtonum(val,
		    IF_HDRPRIO_MIN, IF_HDRPRIO_MAX, &errmsg);
		if (errmsg)
			errx(1, "tx prio %s: %s", val, errmsg);
	}

	if (ioctl(sock, SIOCSTXHPRIO, (caddr_t)&ifr) == -1)
		warn("SIOCSTXHPRIO");
}

void
getrxprio(struct ifencap *ife)
{
	if (strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name)) >=
	    sizeof(ifr.ifr_name))
		errx(1, "hdr prio: name is too long");

	if (ioctl(sock, SIOCGRXHPRIO, (caddr_t)&ifr) == -1)
		return;

	ife->ife_flags |= IFE_RXHPRIO_SET;
	ife->ife_rxhprio = ifr.ifr_hdrprio;
}

void
setrxprio(const char *val, int d)
{
	const char *errmsg = NULL;

	if (strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name)) >=
	    sizeof(ifr.ifr_name))
		errx(1, "rx prio: name is too long");

	if (strcmp(val, "packet") == 0)
		ifr.ifr_hdrprio = IF_HDRPRIO_PACKET;
	else if (strcmp(val, "payload") == 0)
		ifr.ifr_hdrprio = IF_HDRPRIO_PAYLOAD;
	else if (strcmp(val, "outer") == 0)
		ifr.ifr_hdrprio = IF_HDRPRIO_OUTER;
	else {
		ifr.ifr_hdrprio = strtonum(val,
		    IF_HDRPRIO_MIN, IF_HDRPRIO_MAX, &errmsg);
		if (errmsg)
			errx(1, "rx prio %s: %s", val, errmsg);
	}

	if (ioctl(sock, SIOCSRXHPRIO, (caddr_t)&ifr) == -1)
		warn("SIOCSRXHPRIO");
}
#endif

void
getencap(void)
{
	struct ifencap ife = { .ife_flags = 0 };

	getvnetid(&ife);
	getvnetflowid(&ife);
	getifparent(&ife);
#ifndef SMALL
	gettxprio(&ife);
	getrxprio(&ife);
#endif

	if (ife.ife_flags == 0)
		return;

	printf("\tencap:");

	switch (ife.ife_flags & IFE_VNETID_MASK) {
	case IFE_VNETID_NONE:
		printf(" vnetid none");
		break;
	case IFE_VNETID_ANY:
		printf(" vnetid any");
		break;
	case IFE_VNETID_SET:
		printf(" vnetid %lld", ife.ife_vnetid);
		if (ife.ife_flags & IFE_VNETFLOWID)
			printf("+");
		break;
	}

	switch (ife.ife_flags & IFE_PARENT_MASK) {
	case IFE_PARENT_NONE:
		printf(" parent none");
		break;
	case IFE_PARENT_SET:
		printf(" parent %s", ife.ife_parent);
		break;
	}

#ifndef SMALL
	if (ife.ife_flags & IFE_TXHPRIO_SET) {
		printf(" txprio ");
		switch (ife.ife_txhprio) {
		case IF_HDRPRIO_PACKET:
			printf("packet");
			break;
		case IF_HDRPRIO_PAYLOAD:
			printf("payload");
			break;
		default:
			printf("%d", ife.ife_txhprio);
			break;
		}
	}

	if (ife.ife_flags & IFE_RXHPRIO_SET) {
		printf(" rxprio ");
		switch (ife.ife_rxhprio) {
		case IF_HDRPRIO_PACKET:
			printf("packet");
			break;
		case IF_HDRPRIO_PAYLOAD:
			printf("payload");
			break;
		case IF_HDRPRIO_OUTER:
			printf("outer");
			break;
		default:
			printf("%d", ife.ife_rxhprio);
			break;
		}
	}
#endif

	printf("\n");
}

void
settrunkport(const char *val, int d)
{
	struct trunk_reqport rp;

	bzero(&rp, sizeof(rp));
	strlcpy(rp.rp_ifname, ifname, sizeof(rp.rp_ifname));
	strlcpy(rp.rp_portname, val, sizeof(rp.rp_portname));

	if (ioctl(sock, SIOCSTRUNKPORT, &rp) == -1)
		err(1, "%s %s: SIOCSTRUNKPORT", rp.rp_ifname, rp.rp_portname);
}

void
unsettrunkport(const char *val, int d)
{
	struct trunk_reqport rp;

	bzero(&rp, sizeof(rp));
	strlcpy(rp.rp_ifname, ifname, sizeof(rp.rp_ifname));
	strlcpy(rp.rp_portname, val, sizeof(rp.rp_portname));

	if (ioctl(sock, SIOCSTRUNKDELPORT, &rp) == -1)
		err(1, "%s: %s: SIOCSTRUNKDELPORT", rp.rp_ifname,
		    rp.rp_portname);
}

void
settrunkproto(const char *val, int d)
{
	struct trunk_protos tpr[] = TRUNK_PROTOS;
	struct trunk_reqall ra;
	int i;

	bzero(&ra, sizeof(ra));
	ra.ra_proto = TRUNK_PROTO_MAX;

	for (i = 0; i < (sizeof(tpr) / sizeof(tpr[0])); i++) {
		if (strcmp(val, tpr[i].tpr_name) == 0) {
			ra.ra_proto = tpr[i].tpr_proto;
			break;
		}
	}
	if (ra.ra_proto == TRUNK_PROTO_MAX)
		errx(1, "Invalid trunk protocol: %s", val);

	strlcpy(ra.ra_ifname, ifname, sizeof(ra.ra_ifname));
	if (ioctl(sock, SIOCSTRUNK, &ra) != 0)
		err(1, "%s: SIOCSTRUNK", ra.ra_ifname);
}

void
settrunklacpmode(const char *val, int d)
{
	struct trunk_reqall ra;
	struct trunk_opts tops;

	bzero(&ra, sizeof(ra));
	strlcpy(ra.ra_ifname, ifname, sizeof(ra.ra_ifname));

	if (ioctl(sock, SIOCGTRUNK, &ra) != 0)
		err(1, "%s: SIOCGTRUNK", ra.ra_ifname);

	if (ra.ra_proto != TRUNK_PROTO_LACP)
		errx(1, "Invalid option for trunk: %s", ifname);

	if (strcmp(val, lacpmodeactive) != 0 &&
	    strcmp(val, lacpmodepassive) != 0)
		errx(1, "Invalid lacpmode option for trunk: %s", ifname);

	bzero(&tops, sizeof(tops));
	strlcpy(tops.to_ifname, ifname, sizeof(tops.to_ifname));
	tops.to_proto = TRUNK_PROTO_LACP;
	tops.to_opts |= TRUNK_OPT_LACP_MODE;

	if (strcmp(val, lacpmodeactive) == 0)
		tops.to_lacpopts.lacp_mode = 1;
	else
		tops.to_lacpopts.lacp_mode = 0;

	if (ioctl(sock, SIOCSTRUNKOPTS, &tops) != 0)
		err(1, "%s: SIOCSTRUNKOPTS", tops.to_ifname);
}

void
settrunklacptimeout(const char *val, int d)
{
	struct trunk_reqall ra;
	struct trunk_opts tops;

	bzero(&ra, sizeof(ra));
	strlcpy(ra.ra_ifname, ifname, sizeof(ra.ra_ifname));

	if (ioctl(sock, SIOCGTRUNK, &ra) != 0)
		err(1, "%s SIOCGTRUNK", ra.ra_ifname);

	if (ra.ra_proto != TRUNK_PROTO_LACP)
		errx(1, "Invalid option for trunk: %s", ifname);

	if (strcmp(val, lacptimeoutfast) != 0 &&
	    strcmp(val, lacptimeoutslow) != 0)
		errx(1, "Invalid lacptimeout option for trunk: %s", ifname);

	bzero(&tops, sizeof(tops));
	strlcpy(tops.to_ifname, ifname, sizeof(tops.to_ifname));
	tops.to_proto = TRUNK_PROTO_LACP;
	tops.to_opts |= TRUNK_OPT_LACP_TIMEOUT;

	if (strcmp(val, lacptimeoutfast) == 0)
		tops.to_lacpopts.lacp_timeout = 1;
	else
		tops.to_lacpopts.lacp_timeout = 0;

	if (ioctl(sock, SIOCSTRUNKOPTS, &tops) != 0)
		err(1, "%s: SIOCSTRUNKOPTS", tops.to_ifname);
}

void
trunk_status(void)
{
	struct trunk_protos tpr[] = TRUNK_PROTOS;
	struct trunk_reqport rp, rpbuf[TRUNK_MAX_PORTS];
	struct trunk_reqall ra;
	struct lacp_opreq *lp;
	const char *proto = "<unknown>";
	int i, isport = 0;

	bzero(&rp, sizeof(rp));
	bzero(&ra, sizeof(ra));

	strlcpy(rp.rp_ifname, ifname, sizeof(rp.rp_ifname));
	strlcpy(rp.rp_portname, ifname, sizeof(rp.rp_portname));

	if (ioctl(sock, SIOCGTRUNKPORT, &rp) == 0)
		isport = 1;

	strlcpy(ra.ra_ifname, ifname, sizeof(ra.ra_ifname));
	ra.ra_size = sizeof(rpbuf);
	ra.ra_port = rpbuf;

	if (ioctl(sock, SIOCGTRUNK, &ra) == 0) {
		lp = (struct lacp_opreq *)&ra.ra_lacpreq;

		for (i = 0; i < (sizeof(tpr) / sizeof(tpr[0])); i++) {
			if (ra.ra_proto == tpr[i].tpr_proto) {
				proto = tpr[i].tpr_name;
				break;
			}
		}

		printf("\ttrunk: trunkproto %s", proto);
		if (isport)
			printf(" trunkdev %s", rp.rp_ifname);
		putchar('\n');
		if (ra.ra_proto == TRUNK_PROTO_LACP) {
			char *act_mac = strdup(
			    ether_ntoa((struct ether_addr*)lp->actor_mac));
			if (act_mac == NULL)
				err(1, "strdup");
			printf("\ttrunk id: [(%04X,%s,%04X,%04X,%04X),\n"
			    "\t\t (%04X,%s,%04X,%04X,%04X)]\n",
			    lp->actor_prio, act_mac,
			    lp->actor_key, lp->actor_portprio, lp->actor_portno,
			    lp->partner_prio,
			    ether_ntoa((struct ether_addr*)lp->partner_mac),
			    lp->partner_key, lp->partner_portprio,
			    lp->partner_portno);
			free(act_mac);
		}

		for (i = 0; i < ra.ra_ports; i++) {
			lp = (struct lacp_opreq *)&(rpbuf[i].rp_lacpreq);
			if (ra.ra_proto == TRUNK_PROTO_LACP) {
				printf("\t\t%s lacp actor "
				    "system pri 0x%x mac %s, key 0x%x, "
				    "port pri 0x%x number 0x%x\n",
				    rpbuf[i].rp_portname,
				    lp->actor_prio,
				    ether_ntoa((struct ether_addr*)
				     lp->actor_mac),
				    lp->actor_key,
				    lp->actor_portprio, lp->actor_portno);
				printf("\t\t%s lacp actor state ",
				    rpbuf[i].rp_portname);
				printb_status(lp->actor_state,
				    LACP_STATE_BITS);
				putchar('\n');

				printf("\t\t%s lacp partner "
				    "system pri 0x%x mac %s, key 0x%x, "
				    "port pri 0x%x number 0x%x\n",
				    rpbuf[i].rp_portname,
				    lp->partner_prio,
				    ether_ntoa((struct ether_addr*)
				     lp->partner_mac),
				    lp->partner_key,
				    lp->partner_portprio, lp->partner_portno);
				printf("\t\t%s lacp partner state ",
				    rpbuf[i].rp_portname);
				printb_status(lp->partner_state,
				    LACP_STATE_BITS);
				putchar('\n');
			}

			printf("\t\t%s port ", rpbuf[i].rp_portname);
			printb_status(rpbuf[i].rp_flags, TRUNK_PORT_BITS);
			putchar('\n');
		}

		if (showmediaflag) {
			printf("\tsupported trunk protocols:\n");
			for (i = 0; i < (sizeof(tpr) / sizeof(tpr[0])); i++)
				printf("\t\ttrunkproto %s\n", tpr[i].tpr_name);
		}
	} else if (isport)
		printf("\ttrunk: trunkdev %s\n", rp.rp_ifname);
}

#ifndef SMALL
static const char *carp_states[] = { CARP_STATES };
static const char *carp_bal_modes[] = { CARP_BAL_MODES };

void
carp_status(void)
{
	const char *state, *balmode;
	struct carpreq carpr;
	char peer[32];
	int i;

	memset((char *)&carpr, 0, sizeof(struct carpreq));
	ifr.ifr_data = (caddr_t)&carpr;

	if (ioctl(sock, SIOCGVH, (caddr_t)&ifr) == -1)
		return;

	if (carpr.carpr_vhids[0] == 0)
		return;

	if (carpr.carpr_balancing > CARP_BAL_MAXID)
		balmode = "<UNKNOWN>";
	else
		balmode = carp_bal_modes[carpr.carpr_balancing];

	if (carpr.carpr_peer.s_addr != htonl(INADDR_CARP_GROUP))
		snprintf(peer, sizeof(peer),
		    " carppeer %s", inet_ntoa(carpr.carpr_peer));
	else
		peer[0] = '\0';

	for (i = 0; carpr.carpr_vhids[i]; i++) {
		if (carpr.carpr_states[i] > CARP_MAXSTATE)
			state = "<UNKNOWN>";
		else
			state = carp_states[carpr.carpr_states[i]];
		if (carpr.carpr_vhids[1] == 0) {
			printf("\tcarp: %s carpdev %s vhid %u advbase %d "
			    "advskew %u%s\n", state,
			    carpr.carpr_carpdev[0] != '\0' ?
			    carpr.carpr_carpdev : "none", carpr.carpr_vhids[0],
			    carpr.carpr_advbase, carpr.carpr_advskews[0],
			    peer);
		} else {
			if (i == 0) {
				printf("\tcarp: carpdev %s advbase %d"
				    " balancing %s%s\n",
				    carpr.carpr_carpdev[0] != '\0' ?
				    carpr.carpr_carpdev : "none",
				    carpr.carpr_advbase, balmode, peer);
			}
			printf("\t\tstate %s vhid %u advskew %u\n", state,
			    carpr.carpr_vhids[i], carpr.carpr_advskews[i]);
		}
	}
}

void
setcarp_passwd(const char *val, int d)
{
	struct carpreq carpr;

	bzero(&carpr, sizeof(struct carpreq));
	ifr.ifr_data = (caddr_t)&carpr;

	if (ioctl(sock, SIOCGVH, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCGVH", ifr.ifr_name);

	bzero(carpr.carpr_key, CARP_KEY_LEN);
	strlcpy((char *)carpr.carpr_key, val, CARP_KEY_LEN);

	if (ioctl(sock, SIOCSVH, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCSVH", ifr.ifr_name);
}

void
setcarp_vhid(const char *val, int d)
{
	const char *errmsg = NULL;
	struct carpreq carpr;
	int vhid;

	vhid = strtonum(val, 1, 255, &errmsg);
	if (errmsg)
		errx(1, "vhid %s: %s", val, errmsg);

	bzero(&carpr, sizeof(struct carpreq));
	ifr.ifr_data = (caddr_t)&carpr;

	if (ioctl(sock, SIOCGVH, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCGVH", ifr.ifr_name);

	carpr.carpr_vhids[0] = vhid;
	carpr.carpr_vhids[1] = 0;

	if (ioctl(sock, SIOCSVH, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCSVH", ifr.ifr_name);
}

void
setcarp_advskew(const char *val, int d)
{
	const char *errmsg = NULL;
	struct carpreq carpr;
	int advskew;

	advskew = strtonum(val, 0, 254, &errmsg);
	if (errmsg)
		errx(1, "advskew %s: %s", val, errmsg);

	bzero(&carpr, sizeof(struct carpreq));
	ifr.ifr_data = (caddr_t)&carpr;

	if (ioctl(sock, SIOCGVH, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCGVH", ifr.ifr_name);

	carpr.carpr_advskews[0] = advskew;

	if (ioctl(sock, SIOCSVH, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCSVH", ifr.ifr_name);
}

void
setcarp_advbase(const char *val, int d)
{
	const char *errmsg = NULL;
	struct carpreq carpr;
	int advbase;

	advbase = strtonum(val, 0, 254, &errmsg);
	if (errmsg)
		errx(1, "advbase %s: %s", val, errmsg);

	bzero(&carpr, sizeof(struct carpreq));
	ifr.ifr_data = (caddr_t)&carpr;

	if (ioctl(sock, SIOCGVH, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCGVH", ifr.ifr_name);

	carpr.carpr_advbase = advbase;

	if (ioctl(sock, SIOCSVH, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCSVH", ifr.ifr_name);
}

void
setcarppeer(const char *val, int d)
{
	struct carpreq carpr;
	struct addrinfo hints, *peerres;
	int ecode;

	bzero(&carpr, sizeof(struct carpreq));
	ifr.ifr_data = (caddr_t)&carpr;

	if (ioctl(sock, SIOCGVH, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCGVH", ifr.ifr_name);

	bzero(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;

	if ((ecode = getaddrinfo(val, NULL, &hints, &peerres)) != 0)
		errx(1, "error in parsing address string: %s",
		    gai_strerror(ecode));

	if (peerres->ai_addr->sa_family != AF_INET)
		errx(1, "only IPv4 addresses supported for the carppeer");

	carpr.carpr_peer.s_addr = ((struct sockaddr_in *)
	    peerres->ai_addr)->sin_addr.s_addr;

	if (ioctl(sock, SIOCSVH, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCSVH", ifr.ifr_name);

	freeaddrinfo(peerres);
}

void
unsetcarppeer(const char *val, int d)
{
	struct carpreq carpr;

	bzero(&carpr, sizeof(struct carpreq));
	ifr.ifr_data = (caddr_t)&carpr;

	if (ioctl(sock, SIOCGVH, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCGVH", ifr.ifr_name);

	bzero(&carpr.carpr_peer, sizeof(carpr.carpr_peer));

	if (ioctl(sock, SIOCSVH, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCSVH", ifr.ifr_name);
}

void
setcarp_state(const char *val, int d)
{
	struct carpreq carpr;
	int i;

	bzero(&carpr, sizeof(struct carpreq));
	ifr.ifr_data = (caddr_t)&carpr;

	if (ioctl(sock, SIOCGVH, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCGVH", ifr.ifr_name);

	for (i = 0; i <= CARP_MAXSTATE; i++) {
		if (!strcasecmp(val, carp_states[i])) {
			carpr.carpr_state = i;
			break;
		}
	}

	if (ioctl(sock, SIOCSVH, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCSVH", ifr.ifr_name);
}

void
setcarpdev(const char *val, int d)
{
	struct carpreq carpr;

	bzero(&carpr, sizeof(struct carpreq));
	ifr.ifr_data = (caddr_t)&carpr;

	if (ioctl(sock, SIOCGVH, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCGVH", ifr.ifr_name);

	strlcpy(carpr.carpr_carpdev, val, sizeof(carpr.carpr_carpdev));

	if (ioctl(sock, SIOCSVH, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCSVH", ifr.ifr_name);
}

void
setcarp_nodes(const char *val, int d)
{
	char *optlist, *str;
	int i;
	struct carpreq carpr;

	bzero(&carpr, sizeof(struct carpreq));
	ifr.ifr_data = (caddr_t)&carpr;

	if (ioctl(sock, SIOCGVH, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCGVH", ifr.ifr_name);

	bzero(carpr.carpr_vhids, sizeof(carpr.carpr_vhids));
	bzero(carpr.carpr_advskews, sizeof(carpr.carpr_advskews));

	optlist = strdup(val);
	if (optlist == NULL)
		err(1, "strdup");

	str = strtok(optlist, ",");
	for (i = 0; str != NULL; i++) {
		u_int vhid, advskew;

		if (i >= CARP_MAXNODES)
			errx(1, "too many carp nodes");
		if (sscanf(str, "%u:%u", &vhid, &advskew) != 2) {
			errx(1, "non parsable arg: %s", str);
		}
		if (vhid > 255)
			errx(1, "vhid %u: value too large", vhid);
		if (advskew >= 255)
			errx(1, "advskew %u: value too large", advskew);

		carpr.carpr_vhids[i] = vhid;
		carpr.carpr_advskews[i] = advskew;
		str = strtok(NULL, ",");
	}
	free(optlist);

	if (ioctl(sock, SIOCSVH, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCSVH", ifr.ifr_name);
}

void
setcarp_balancing(const char *val, int d)
{
	int i;
	struct carpreq carpr;

	bzero(&carpr, sizeof(struct carpreq));
	ifr.ifr_data = (caddr_t)&carpr;

	if (ioctl(sock, SIOCGVH, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCGVH", ifr.ifr_name);

	for (i = 0; i <= CARP_BAL_MAXID; i++)
		if (!strcasecmp(val, carp_bal_modes[i]))
			break;

	if (i > CARP_BAL_MAXID)
		errx(1, "balancing %s: unknown mode", val);

	carpr.carpr_balancing = i;

	if (ioctl(sock, SIOCSVH, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCSVH", ifr.ifr_name);
}

void
setpfsync_syncdev(const char *val, int d)
{
	struct pfsyncreq preq;

	bzero(&preq, sizeof(struct pfsyncreq));
	ifr.ifr_data = (caddr_t)&preq;

	if (ioctl(sock, SIOCGETPFSYNC, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCGETPFSYNC", ifr.ifr_name);

	strlcpy(preq.pfsyncr_syncdev, val, sizeof(preq.pfsyncr_syncdev));

	if (ioctl(sock, SIOCSETPFSYNC, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCSETPFSYNC", ifr.ifr_name);
}

void
unsetpfsync_syncdev(const char *val, int d)
{
	struct pfsyncreq preq;

	bzero(&preq, sizeof(struct pfsyncreq));
	ifr.ifr_data = (caddr_t)&preq;

	if (ioctl(sock, SIOCGETPFSYNC, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCGETPFSYNC", ifr.ifr_name);

	bzero(&preq.pfsyncr_syncdev, sizeof(preq.pfsyncr_syncdev));

	if (ioctl(sock, SIOCSETPFSYNC, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCSETPFSYNC", ifr.ifr_name);
}

void
setpfsync_syncpeer(const char *val, int d)
{
	struct pfsyncreq preq;
	struct addrinfo hints, *peerres;
	int ecode;

	bzero(&preq, sizeof(struct pfsyncreq));
	ifr.ifr_data = (caddr_t)&preq;

	if (ioctl(sock, SIOCGETPFSYNC, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCGETPFSYNC", ifr.ifr_name);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;	/*dummy*/

	if ((ecode = getaddrinfo(val, NULL, &hints, &peerres)) != 0)
		errx(1, "error in parsing address string: %s",
		    gai_strerror(ecode));

	if (peerres->ai_addr->sa_family != AF_INET)
		errx(1, "only IPv4 addresses supported for the syncpeer");

	preq.pfsyncr_syncpeer.s_addr = ((struct sockaddr_in *)
	    peerres->ai_addr)->sin_addr.s_addr;

	if (ioctl(sock, SIOCSETPFSYNC, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCSETPFSYNC", ifr.ifr_name);

	freeaddrinfo(peerres);
}

void
unsetpfsync_syncpeer(const char *val, int d)
{
	struct pfsyncreq preq;

	bzero(&preq, sizeof(struct pfsyncreq));
	ifr.ifr_data = (caddr_t)&preq;

	if (ioctl(sock, SIOCGETPFSYNC, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCGETPFSYNC", ifr.ifr_name);

	preq.pfsyncr_syncpeer.s_addr = 0;

	if (ioctl(sock, SIOCSETPFSYNC, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCSETPFSYNC", ifr.ifr_name);
}

void
setpfsync_maxupd(const char *val, int d)
{
	const char *errmsg = NULL;
	struct pfsyncreq preq;
	int maxupdates;

	maxupdates = strtonum(val, 0, 255, &errmsg);
	if (errmsg)
		errx(1, "maxupd %s: %s", val, errmsg);

	bzero(&preq, sizeof(struct pfsyncreq));
	ifr.ifr_data = (caddr_t)&preq;

	if (ioctl(sock, SIOCGETPFSYNC, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCGETPFSYNC", ifr.ifr_name);

	preq.pfsyncr_maxupdates = maxupdates;

	if (ioctl(sock, SIOCSETPFSYNC, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCSETPFSYNC", ifr.ifr_name);
}

void
setpfsync_defer(const char *val, int d)
{
	struct pfsyncreq preq;

	bzero(&preq, sizeof(struct pfsyncreq));
	ifr.ifr_data = (caddr_t)&preq;

	if (ioctl(sock, SIOCGETPFSYNC, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCGETPFSYNC", ifr.ifr_name);

	preq.pfsyncr_defer = d;
	if (ioctl(sock, SIOCSETPFSYNC, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCSETPFSYNC", ifr.ifr_name);
}

void
pfsync_status(void)
{
	struct pfsyncreq preq;

	bzero(&preq, sizeof(struct pfsyncreq));
	ifr.ifr_data = (caddr_t)&preq;

	if (ioctl(sock, SIOCGETPFSYNC, (caddr_t)&ifr) == -1)
		return;

	if (preq.pfsyncr_syncdev[0] != '\0') {
		printf("\tpfsync: syncdev: %s ", preq.pfsyncr_syncdev);
		if (preq.pfsyncr_syncpeer.s_addr != htonl(INADDR_PFSYNC_GROUP))
			printf("syncpeer: %s ",
			    inet_ntoa(preq.pfsyncr_syncpeer));
		printf("maxupd: %d ", preq.pfsyncr_maxupdates);
		printf("defer: %s\n", preq.pfsyncr_defer ? "on" : "off");
	}
}

void
pflow_status(void)
{
	struct pflowreq		 preq;
	struct sockaddr_in	*sin;
	struct sockaddr_in6	*sin6;
	int			 error;
	char			 buf[INET6_ADDRSTRLEN];

	bzero(&preq, sizeof(struct pflowreq));
	ifr.ifr_data = (caddr_t)&preq;

	if (ioctl(sock, SIOCGETPFLOW, (caddr_t)&ifr) == -1)
		 return;

	if (preq.flowsrc.ss_family == AF_INET || preq.flowsrc.ss_family ==
	    AF_INET6) {
		error = getnameinfo((struct sockaddr*)&preq.flowsrc,
		    preq.flowsrc.ss_len, buf, sizeof(buf), NULL, 0,
		    NI_NUMERICHOST);
		if (error)
			err(1, "sender: %s", gai_strerror(error));
	}

	printf("\tpflow: ");
	switch (preq.flowsrc.ss_family) {
	case AF_INET:
		sin = (struct sockaddr_in*) &preq.flowsrc;
		if (sin->sin_addr.s_addr != INADDR_ANY) {
			printf("sender: %s", buf);
			if (sin->sin_port != 0)
				printf(":%u", ntohs(sin->sin_port));
			printf(" ");
		}
		break;
	case AF_INET6:
		sin6 = (struct sockaddr_in6*) &preq.flowsrc;
		if (!IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr)) {
			printf("sender: [%s]", buf);
			if (sin6->sin6_port != 0)
				printf(":%u", ntohs(sin6->sin6_port));
			printf(" ");
		}
	default:
		break;
	}
	if (preq.flowdst.ss_family == AF_INET || preq.flowdst.ss_family ==
	    AF_INET6) {
		error = getnameinfo((struct sockaddr*)&preq.flowdst,
		    preq.flowdst.ss_len, buf, sizeof(buf), NULL, 0,
		    NI_NUMERICHOST);
		if (error)
			err(1, "receiver: %s", gai_strerror(error));
	}
	switch (preq.flowdst.ss_family) {
	case AF_INET:
		sin = (struct sockaddr_in*)&preq.flowdst;
		printf("receiver: %s:", sin->sin_addr.s_addr != INADDR_ANY ?
		    buf : "INVALID");
		if (sin->sin_port == 0)
			printf("%s ", "INVALID");
		else
			printf("%u ", ntohs(sin->sin_port));
		break;
	case AF_INET6:
		sin6 = (struct sockaddr_in6*) &preq.flowdst;
		printf("receiver: [%s]:",
		    !IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr) ? buf :
		    "INVALID");
		if (sin6->sin6_port == 0)
			printf("%s ", "INVALID");
		else
			printf("%u ", ntohs(sin6->sin6_port));
		break;
	default:
		printf("receiver: INVALID:INVALID ");
		break;
	}
	printf("version: %d\n", preq.version);
}

void
pflow_addr(const char *val, struct sockaddr_storage *ss) {
	struct addrinfo hints, *res0;
	int error, flag;
	char *cp, *ip, *port, buf[HOST_NAME_MAX+1 + sizeof (":65535")];

	if (strlcpy(buf, val, sizeof(buf)) >= sizeof(buf))
		errx(1, "%s bad value", val);

	port = NULL;
	cp = buf;
	if (*cp == '[')
		flag = 1;
	else
		flag = 0;

	for(; *cp; ++cp) {
		if (*cp == ']' && *(cp + 1) == ':' && flag) {
			*cp = '\0';
			*(cp + 1) = '\0';
			port = cp + 2;
			break;
		}
		if (*cp == ']' && *(cp + 1) == '\0' && flag) {
			*cp = '\0';
			port = NULL;
			break;
		}
		if (*cp == ':' && !flag) {
			*cp = '\0';
			port = cp + 1;
			break;
		}
	}

	ip = buf;
	if (flag)
		ip++;

	bzero(&hints, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM; /*dummy*/
	hints.ai_flags = AI_NUMERICHOST;

	if ((error = getaddrinfo(ip, port, &hints, &res0)) != 0)
		errx(1, "error in parsing address string: %s",
		    gai_strerror(error));

	memcpy(ss, res0->ai_addr, res0->ai_addr->sa_len);
	freeaddrinfo(res0);
}

void
setpflow_sender(const char *val, int d)
{
	struct pflowreq preq;

	bzero(&preq, sizeof(struct pflowreq));
	ifr.ifr_data = (caddr_t)&preq;
	preq.addrmask |= PFLOW_MASK_SRCIP;
	pflow_addr(val, &preq.flowsrc);

	if (ioctl(sock, SIOCSETPFLOW, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCSETPFLOW", ifr.ifr_name);
}

void
unsetpflow_sender(const char *val, int d)
{
	struct pflowreq preq;

	bzero(&preq, sizeof(struct pflowreq));
	preq.addrmask |= PFLOW_MASK_SRCIP;
	ifr.ifr_data = (caddr_t)&preq;
	if (ioctl(sock, SIOCSETPFLOW, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCSETPFLOW", ifr.ifr_name);
}

void
setpflow_receiver(const char *val, int d)
{
	struct pflowreq preq;

	bzero(&preq, sizeof(struct pflowreq));
	ifr.ifr_data = (caddr_t)&preq;
	preq.addrmask |= PFLOW_MASK_DSTIP;
	pflow_addr(val, &preq.flowdst);

	if (ioctl(sock, SIOCSETPFLOW, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCSETPFLOW", ifr.ifr_name);
}

void
unsetpflow_receiver(const char *val, int d)
{
	struct pflowreq preq;

	bzero(&preq, sizeof(struct pflowreq));
	ifr.ifr_data = (caddr_t)&preq;
	preq.addrmask |= PFLOW_MASK_DSTIP;
	if (ioctl(sock, SIOCSETPFLOW, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCSETPFLOW", ifr.ifr_name);
}

/* PFLOWPROTO XXX */
void
setpflowproto(const char *val, int d)
{
	struct pflow_protos ppr[] = PFLOW_PROTOS;
	struct pflowreq preq;
	int i;

	bzero(&preq, sizeof(preq));
	preq.version = PFLOW_PROTO_MAX;

	for (i = 0; i < (sizeof(ppr) / sizeof(ppr[0])); i++) {
		if (strcmp(val, ppr[i].ppr_name) == 0) {
			preq.version = ppr[i].ppr_proto;
			break;
		}
	}
	if (preq.version == PFLOW_PROTO_MAX)
		errx(1, "Invalid pflow protocol: %s", val);

	preq.addrmask |= PFLOW_MASK_VERSION;

	ifr.ifr_data = (caddr_t)&preq;

	if (ioctl(sock, SIOCSETPFLOW, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCSETPFLOW", ifr.ifr_name);
}

void
pppoe_status(void)
{
	struct pppoediscparms parms;
	struct pppoeconnectionstate state;

	memset(&state, 0, sizeof(state));

	strlcpy(parms.ifname, ifname, sizeof(parms.ifname));
	if (ioctl(sock, PPPOEGETPARMS, &parms) == -1)
		return;

	printf("\tdev: %s ", parms.eth_ifname);

	if (*parms.ac_name)
		printf("ac: %s ", parms.ac_name);
	if (*parms.service_name)
		printf("svc: %s ", parms.service_name);

	strlcpy(state.ifname, ifname, sizeof(state.ifname));
	if (ioctl(sock, PPPOEGETSESSION, &state) == -1)
		err(1, "PPPOEGETSESSION");

	printf("state: ");
	switch (state.state) {
	case PPPOE_STATE_INITIAL:
		printf("initial"); break;
	case PPPOE_STATE_PADI_SENT:
		printf("PADI sent"); break;
	case PPPOE_STATE_PADR_SENT:
		printf("PADR sent"); break;
	case PPPOE_STATE_SESSION:
		printf("session"); break;
	case PPPOE_STATE_CLOSING:
		printf("closing"); break;
	}
	printf("\n\tsid: 0x%x", state.session_id);
	printf(" PADI retries: %d", state.padi_retry_no);
	printf(" PADR retries: %d", state.padr_retry_no);

	if (state.state == PPPOE_STATE_SESSION) {
		struct timespec temp_time;
		time_t diff_time, day = 0;
		unsigned int hour = 0, min = 0, sec = 0;

		if (state.session_time.tv_sec != 0) {
			if (clock_gettime(CLOCK_BOOTTIME, &temp_time) == -1)
				goto notime;
			diff_time = temp_time.tv_sec -
			    state.session_time.tv_sec;

			day = diff_time / (60 * 60 * 24);
			diff_time %= (60 * 60 * 24);

			hour = diff_time / (60 * 60);
			diff_time %= (60 * 60);

			min = diff_time / 60;
			diff_time %= 60;

			sec = diff_time;
		}
		printf(" time: ");
		if (day != 0)
			printf("%lldd ", (long long)day);
		printf("%02u:%02u:%02u", hour, min, sec);
	}
notime:
	putchar('\n');
}

void
setpppoe_dev(const char *val, int d)
{
	struct pppoediscparms parms;

	strlcpy(parms.ifname, ifname, sizeof(parms.ifname));
	if (ioctl(sock, PPPOEGETPARMS, &parms) == -1)
		return;

	strlcpy(parms.eth_ifname, val, sizeof(parms.eth_ifname));

	if (ioctl(sock, PPPOESETPARMS, &parms) == -1)
		err(1, "PPPOESETPARMS");
}

void
setpppoe_svc(const char *val, int d)
{
	struct pppoediscparms parms;

	strlcpy(parms.ifname, ifname, sizeof(parms.ifname));
	if (ioctl(sock, PPPOEGETPARMS, &parms) == -1)
		return;

	if (d == 0)
		strlcpy(parms.service_name, val, sizeof(parms.service_name));
	else
		memset(parms.service_name, 0, sizeof(parms.service_name));

	if (ioctl(sock, PPPOESETPARMS, &parms) == -1)
		err(1, "PPPOESETPARMS");
}

void
setpppoe_ac(const char *val, int d)
{
	struct pppoediscparms parms;

	strlcpy(parms.ifname, ifname, sizeof(parms.ifname));
	if (ioctl(sock, PPPOEGETPARMS, &parms) == -1)
		return;

	if (d == 0)
		strlcpy(parms.ac_name, val, sizeof(parms.ac_name));
	else
		memset(parms.ac_name, 0, sizeof(parms.ac_name));

	if (ioctl(sock, PPPOESETPARMS, &parms) == -1)
		err(1, "PPPOESETPARMS");
}

void
spppauthinfo(struct sauthreq *spa, int d)
{
	bzero(spa, sizeof(struct sauthreq));

	ifr.ifr_data = (caddr_t)spa;
	spa->cmd = d == 0 ? SPPPIOGMAUTH : SPPPIOGHAUTH;
	if (ioctl(sock, SIOCGSPPPPARAMS, &ifr) == -1)
		err(1, "%s: SIOCGSPPPPARAMS(SPPPIOGXAUTH)", ifr.ifr_name);
}

void
spppdnsinfo(struct sdnsreq *spd)
{
	memset(spd, 0, sizeof(*spd));

	ifr.ifr_data = (caddr_t)spd;
	spd->cmd = SPPPIOGDNS;
	if (ioctl(sock, SIOCGSPPPPARAMS, &ifr) == -1)
		err(1, "%s: SIOCGSPPPPARAMS(SPPPIOGDNS)", ifr.ifr_name);
}

void
setspppproto(const char *val, int d)
{
	struct sauthreq spa;

	spppauthinfo(&spa, d);

	if (strcmp(val, "pap") == 0)
		spa.proto = PPP_PAP;
	else if (strcmp(val, "chap") == 0)
		spa.proto = PPP_CHAP;
	else if (strcmp(val, "none") == 0)
		spa.proto = 0;
	else
		errx(1, "setpppproto");

	spa.cmd = d == 0 ? SPPPIOSMAUTH : SPPPIOSHAUTH;
	if (ioctl(sock, SIOCSSPPPPARAMS, &ifr) == -1)
		err(1, "%s: SIOCSSPPPPARAMS(SPPPIOSXAUTH)", ifr.ifr_name);
}

void
setsppppeerproto(const char *val, int d)
{
	setspppproto(val, 1);
}

void
setspppname(const char *val, int d)
{
	struct sauthreq spa;

	spppauthinfo(&spa, d);

	if (spa.proto == 0)
		errx(1, "unspecified protocol");
	if (strlcpy(spa.name, val, sizeof(spa.name)) >= sizeof(spa.name))
		errx(1, "setspppname");

	spa.cmd = d == 0 ? SPPPIOSMAUTH : SPPPIOSHAUTH;
	if (ioctl(sock, SIOCSSPPPPARAMS, &ifr) == -1)
		err(1, "%s: SIOCSSPPPPARAMS(SPPPIOSXAUTH)", ifr.ifr_name);
}

void
setsppppeername(const char *val, int d)
{
	setspppname(val, 1);
}

void
setspppkey(const char *val, int d)
{
	struct sauthreq spa;

	spppauthinfo(&spa, d);

	if (spa.proto == 0)
		errx(1, "unspecified protocol");
	if (strlcpy(spa.secret, val, sizeof(spa.secret)) >= sizeof(spa.secret))
		errx(1, "setspppkey");

	spa.cmd = d == 0 ? SPPPIOSMAUTH : SPPPIOSHAUTH;
	if (ioctl(sock, SIOCSSPPPPARAMS, &ifr) == -1)
		err(1, "%s: SIOCSSPPPPARAMS(SPPPIOSXAUTH)", ifr.ifr_name);
}

void
setsppppeerkey(const char *val, int d)
{
	setspppkey(val, 1);
}

void
setsppppeerflag(const char *val, int d)
{
	struct sauthreq spa;
	int flag;

	spppauthinfo(&spa, 1);

	if (spa.proto == 0)
		errx(1, "unspecified protocol");
	if (strcmp(val, "callin") == 0)
		flag = AUTHFLAG_NOCALLOUT;
	else if (strcmp(val, "norechallenge") == 0)
		flag = AUTHFLAG_NORECHALLENGE;
	else
		errx(1, "setppppeerflags");

	if (d)
		spa.flags &= ~flag;
	else
		spa.flags |= flag;

	spa.cmd = SPPPIOSHAUTH;
	if (ioctl(sock, SIOCSSPPPPARAMS, &ifr) == -1)
		err(1, "%s: SIOCSSPPPPARAMS(SPPPIOSXAUTH)", ifr.ifr_name);
}

void
unsetsppppeerflag(const char *val, int d)
{
	setsppppeerflag(val, 1);
}

void
sppp_printproto(const char *name, struct sauthreq *auth)
{
	if (auth->proto == 0)
		return;
	printf("%sproto ", name);
	switch (auth->proto) {
	case PPP_PAP:
		printf("pap ");
		break;
	case PPP_CHAP:
		printf("chap ");
		break;
	default:
		printf("0x%04x ", auth->proto);
		break;
	}
	if (auth->name[0])
		printf("%sname \"%s\" ", name, auth->name);
	if (auth->secret[0])
		printf("%skey \"%s\" ", name, auth->secret);
}

void
sppp_status(void)
{
	struct spppreq spr;
	struct sauthreq spa;
	struct sdnsreq spd;
	char astr[INET_ADDRSTRLEN];
	int i, n;

	bzero(&spr, sizeof(spr));

	ifr.ifr_data = (caddr_t)&spr;
	spr.cmd = SPPPIOGDEFS;
	if (ioctl(sock, SIOCGSPPPPARAMS, &ifr) == -1) {
		return;
	}

	if (spr.phase == PHASE_DEAD)
		return;
	printf("\tsppp: phase ");
	switch (spr.phase) {
	case PHASE_ESTABLISH:
		printf("establish ");
		break;
	case PHASE_TERMINATE:
		printf("terminate ");
		break;
	case PHASE_AUTHENTICATE:
		printf("authenticate ");
		break;
	case PHASE_NETWORK:
		printf("network ");
		break;
	default:
		printf("illegal ");
		break;
	}

	spppauthinfo(&spa, 0);
	sppp_printproto("auth", &spa);
	spppauthinfo(&spa, 1);
	sppp_printproto("peer", &spa);
	if (spa.flags & AUTHFLAG_NOCALLOUT)
		printf("callin ");
	if (spa.flags & AUTHFLAG_NORECHALLENGE)
		printf("norechallenge ");
	putchar('\n');

	spppdnsinfo(&spd);
	for (i = 0, n = 0; i < IPCP_MAX_DNSSRV; i++) {
		if (spd.dns[i].s_addr == INADDR_ANY)
			break;
		printf("%s %s", n++ ? "" : "\tdns:",
		    inet_ntop(AF_INET, &spd.dns[i], astr, sizeof(astr)));
	}
	if (n)
		printf("\n");
}

void
setkeepalive(const char *timeout, const char *count)
{
	const char *errmsg = NULL;
	struct ifkalivereq ikar;
	int t, c;

	t = strtonum(timeout, 1, 3600, &errmsg);
	if (errmsg)
		errx(1, "keepalive period %s: %s", timeout, errmsg);
	c = strtonum(count, 2, 600, &errmsg);
	if (errmsg)
		errx(1, "keepalive count %s: %s", count, errmsg);

	strlcpy(ikar.ikar_name, ifname, sizeof(ikar.ikar_name));
	ikar.ikar_timeo = t;
	ikar.ikar_cnt = c;
	if (ioctl(sock, SIOCSETKALIVE, (caddr_t)&ikar) == -1)
		warn("SIOCSETKALIVE");
}

void
unsetkeepalive(const char *val, int d)
{
	struct ifkalivereq ikar;

	bzero(&ikar, sizeof(ikar));
	strlcpy(ikar.ikar_name, ifname, sizeof(ikar.ikar_name));
	if (ioctl(sock, SIOCSETKALIVE, (caddr_t)&ikar) == -1)
		warn("SIOCSETKALIVE");
}

void
setifpriority(const char *id, int param)
{
	const char *errmsg = NULL;
	int prio;

	prio = strtonum(id, 0, 15, &errmsg);
	if (errmsg)
		errx(1, "priority %s: %s", id, errmsg);

	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	ifr.ifr_metric = prio;
	if (ioctl(sock, SIOCSIFPRIORITY, (caddr_t)&ifr) == -1)
		warn("SIOCSIFPRIORITY");
}

/*
 * WireGuard configuration
 *
 * WG_BASE64_KEY_LEN specifies the size of a base64 encoded WireGuard key.
 * WG_TMP_KEY_LEN specifies the size of a decoded base64 key. For every 4
 * input (base64) bytes, 3 output bytes wil be produced. The output will be
 * padded with 0 bits, therefore we need more than the regular 32 bytes of
 * space.
 */
#define WG_BASE64_KEY_LEN (4 * ((WG_KEY_LEN + 2) / 3))
#define WG_LOAD_KEY(dst, src, fn_name) do {				\
	uint8_t _tmp[WG_KEY_LEN]; int _r;				\
	if (strlen(src) != WG_BASE64_KEY_LEN)				\
		errx(1, fn_name " (key): invalid length");		\
	if ((_r = b64_pton(src, _tmp, sizeof(_tmp))) != sizeof(_tmp))		\
		errx(1, fn_name " (key): invalid base64 %d/%zu", _r, sizeof(_tmp));		\
	memcpy(dst, _tmp, WG_KEY_LEN);					\
} while (0)

struct wg_data_io	 wgdata = { 0 };
struct wg_interface_io	*wg_interface = NULL;
struct wg_peer_io	*wg_peer = NULL;
struct wg_aip_io	*wg_aip = NULL;

void
ensurewginterface(void)
{
	if (wg_interface != NULL)
		return;
	wgdata.wgd_size = sizeof(*wg_interface);
	wgdata.wgd_interface = wg_interface = calloc(1, wgdata.wgd_size);
	if (wg_interface == NULL)
		err(1, "calloc");
}

void
growwgdata(size_t by)
{
	ptrdiff_t peer_offset, aip_offset;

	if (wg_interface == NULL)
		wgdata.wgd_size = sizeof(*wg_interface);

	peer_offset = (void *)wg_peer - (void *)wg_interface;
	aip_offset = (void *)wg_aip - (void *)wg_interface;

	wgdata.wgd_size += by;
	wgdata.wgd_interface = realloc(wg_interface, wgdata.wgd_size);
	if (wgdata.wgd_interface == NULL)
		err(1, "calloc");
	if (wg_interface == NULL)
		bzero(wgdata.wgd_interface, sizeof(*wg_interface));
	wg_interface = wgdata.wgd_interface;

	if (wg_peer != NULL)
		wg_peer = (void *)wg_interface + peer_offset;
	if (wg_aip != NULL)
		wg_aip = (void *)wg_interface + aip_offset;

	bzero((char *)wg_interface + wgdata.wgd_size - by, by);
}

void
setwgpeer(const char *peerkey_b64, int param)
{
	growwgdata(sizeof(*wg_peer));
	if (wg_aip)
		wg_peer = (struct wg_peer_io *)wg_aip;
	else
		wg_peer = &wg_interface->i_peers[0];
	wg_aip = &wg_peer->p_aips[0];
	wg_peer->p_flags |= WG_PEER_HAS_PUBLIC;
	WG_LOAD_KEY(wg_peer->p_public, peerkey_b64, "wgpeer");
	wg_interface->i_peers_count++;
}

void
setwgpeerdesc(const char *descr, int param)
{
	if (wg_peer == NULL)
		errx(1, "wgdescr: wgpeer not set");
	wg_peer->p_flags |= WG_PEER_SET_DESCRIPTION;
	strlcpy(wg_peer->p_description, descr, IFDESCRSIZE);
}

void
setwgpeeraip(const char *aip, int param)
{
	int res;
	if (wg_peer == NULL)
		errx(1, "wgaip: wgpeer not set");

	growwgdata(sizeof(*wg_aip));

	if ((res = inet_net_pton(AF_INET, aip, &wg_aip->a_ipv4,
	    sizeof(wg_aip->a_ipv4))) != -1) {
		wg_aip->a_af = AF_INET;
	} else if ((res = inet_net_pton(AF_INET6, aip, &wg_aip->a_ipv6,
	    sizeof(wg_aip->a_ipv6))) != -1) {
		wg_aip->a_af = AF_INET6;
	} else {
		errx(1, "wgaip: bad address");
	}

	wg_aip->a_cidr = res;

	wg_peer->p_flags |= WG_PEER_REPLACE_AIPS;
	wg_peer->p_aips_count++;

	wg_aip++;
}

void
setwgpeerep(const char *host, const char *service)
{
	int error;
	struct addrinfo *ai;

	if (wg_peer == NULL)
		errx(1, "wgendpoint: wgpeer not set");

	if ((error = getaddrinfo(host, service, NULL, &ai)) != 0)
		errx(1, "%s", gai_strerror(error));

	wg_peer->p_flags |= WG_PEER_HAS_ENDPOINT;
	memcpy(&wg_peer->p_sa, ai->ai_addr, ai->ai_addrlen);
	freeaddrinfo(ai);
}

void
setwgpeerpsk(const char *psk_b64, int param)
{
	if (wg_peer == NULL)
		errx(1, "wgpsk: wgpeer not set");
	wg_peer->p_flags |= WG_PEER_HAS_PSK;
	WG_LOAD_KEY(wg_peer->p_psk, psk_b64, "wgpsk");
}

void
setwgpeerpka(const char *pka, int param)
{
	const char *errmsg = NULL;
	if (wg_peer == NULL)
		errx(1, "wgpka: wgpeer not set");
	/* 43200 == 12h, reasonable for a 16 bit value */
	wg_peer->p_flags |= WG_PEER_HAS_PKA;
	wg_peer->p_pka = strtonum(pka, 0, 43200, &errmsg);
	if (errmsg)
		errx(1, "wgpka: %s, %s", pka, errmsg);
}

void
setwgport(const char *port, int param)
{
	const char *errmsg = NULL;
	ensurewginterface();
	wg_interface->i_flags |= WG_INTERFACE_HAS_PORT;
	wg_interface->i_port = strtonum(port, 0, 65535, &errmsg);
	if (errmsg)
		errx(1, "wgport: %s, %s", port, errmsg);
}

void
setwgkey(const char *private_b64, int param)
{
	ensurewginterface();
	wg_interface->i_flags |= WG_INTERFACE_HAS_PRIVATE;
	WG_LOAD_KEY(wg_interface->i_private, private_b64, "wgkey");
}

void
setwgrtable(const char *id, int param)
{
	const char *errmsg = NULL;
	ensurewginterface();
	wg_interface->i_flags |= WG_INTERFACE_HAS_RTABLE;
	wg_interface->i_rtable = strtonum(id, 0, RT_TABLEID_MAX, &errmsg);
	if (errmsg)
		errx(1, "wgrtable %s: %s", id, errmsg);
}

void
unsetwgpeer(const char *peerkey_b64, int param)
{
	setwgpeer(peerkey_b64, param);
	wg_peer->p_flags |= WG_PEER_REMOVE;
}

void
unsetwgpeerdesc(const char *descr, int param)
{
	if (wg_peer == NULL)
		errx(1, "wgdescr: wgpeer not set");
	wg_peer->p_flags |= WG_PEER_SET_DESCRIPTION;
	strlcpy(wg_peer->p_description, "", IFDESCRSIZE);
}

void
unsetwgpeerpsk(const char *value, int param)
{
	if (wg_peer == NULL)
		errx(1, "wgpsk: wgpeer not set");
	wg_peer->p_flags |= WG_PEER_HAS_PSK;
	bzero(wg_peer->p_psk, WG_KEY_LEN);
}

void
unsetwgpeerall(const char *value, int param)
{
	ensurewginterface();
	wg_interface->i_flags |= WG_INTERFACE_REPLACE_PEERS;
}

void
process_wg_commands(void)
{
	if (actions & A_WIREGUARD) {
		strlcpy(wgdata.wgd_name, ifname, sizeof(wgdata.wgd_name));

		if (ioctl(sock, SIOCSWG, (caddr_t)&wgdata) == -1)
			err(1, "%s: SIOCSWG", wgdata.wgd_name);
	}
}

void
wg_status(int ifaliases)
{
	size_t			 i, j, last_size;
	struct timespec		 now;
	char			 hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
	char			 key[WG_BASE64_KEY_LEN + 1];

	strlcpy(wgdata.wgd_name, ifname, sizeof(wgdata.wgd_name));
	wgdata.wgd_size = 0;
	wgdata.wgd_interface = NULL;
	for (last_size = wgdata.wgd_size;; last_size = wgdata.wgd_size) {
		if (ioctl(sock, SIOCGWG, (caddr_t)&wgdata) < 0) {
			if (errno == ENOTTY)
				goto out;
			err(1, "%s: SIOCGWG", wgdata.wgd_name);
		}
		if (last_size >= wgdata.wgd_size)
			break;
		wgdata.wgd_interface = realloc(wgdata.wgd_interface,
		    wgdata.wgd_size);
		if (!wgdata.wgd_interface)
			err(1, "realloc");
	}
	wg_interface = wgdata.wgd_interface;

	if (wg_interface->i_flags & WG_INTERFACE_HAS_PORT)
		printf("\twgport %hu\n", wg_interface->i_port);
	if (wg_interface->i_flags & WG_INTERFACE_HAS_RTABLE)
		printf("\twgrtable %d\n", wg_interface->i_rtable);
	if (wg_interface->i_flags & WG_INTERFACE_HAS_PUBLIC) {
		b64_ntop(wg_interface->i_public, WG_KEY_LEN,
		    key, sizeof(key));
		printf("\twgpubkey %s\n", key);
	}

	if (ifaliases) {
		wg_peer = &wg_interface->i_peers[0];
		for (i = 0; i < wg_interface->i_peers_count; i++) {
			b64_ntop(wg_peer->p_public, WG_KEY_LEN,
			    key, sizeof(key));
			printf("\twgpeer %s\n", key);

			if (strlen(wg_peer->p_description))
				printf("\t\twgdescr: %s\n",
				    wg_peer->p_description);

			if (wg_peer->p_flags & WG_PEER_HAS_PSK)
				printf("\t\twgpsk (present)\n");

			if (wg_peer->p_flags & WG_PEER_HAS_PKA && wg_peer->p_pka)
				printf("\t\twgpka %u (sec)\n", wg_peer->p_pka);

			if (wg_peer->p_flags & WG_PEER_HAS_ENDPOINT) {
				if (getnameinfo(&wg_peer->p_sa, wg_peer->p_sa.sa_len,
				    hbuf, sizeof(hbuf), sbuf, sizeof(sbuf),
				    NI_NUMERICHOST | NI_NUMERICSERV) == 0)
					printf("\t\twgendpoint %s %s\n", hbuf, sbuf);
				else
					printf("\t\twgendpoint unable to print\n");
			}

			printf("\t\ttx: %llu, rx: %llu\n",
			    wg_peer->p_txbytes, wg_peer->p_rxbytes);

			if (wg_peer->p_last_handshake.tv_sec != 0) {
				clock_gettime(CLOCK_REALTIME, &now);
				printf("\t\tlast handshake: %lld seconds ago\n",
				    now.tv_sec - wg_peer->p_last_handshake.tv_sec);
			}


			wg_aip = &wg_peer->p_aips[0];
			for (j = 0; j < wg_peer->p_aips_count; j++) {
				inet_ntop(wg_aip->a_af, &wg_aip->a_addr,
				    hbuf, sizeof(hbuf));
				printf("\t\twgaip %s/%d\n", hbuf, wg_aip->a_cidr);
				wg_aip++;
			}
			wg_peer = (struct wg_peer_io *)wg_aip;
		}
	}
out:
	free(wgdata.wgd_interface);
}

const struct umb_valdescr umb_regstate[] = MBIM_REGSTATE_DESCRIPTIONS;
const struct umb_valdescr umb_dataclass[] = MBIM_DATACLASS_DESCRIPTIONS;
const struct umb_valdescr umb_simstate[] = MBIM_SIMSTATE_DESCRIPTIONS;
const struct umb_valdescr umb_istate[] = UMB_INTERNAL_STATE_DESCRIPTIONS;

const struct umb_valdescr umb_classalias[] = {
	{ MBIM_DATACLASS_GPRS | MBIM_DATACLASS_EDGE, "2g" },
	{ MBIM_DATACLASS_UMTS | MBIM_DATACLASS_HSDPA | MBIM_DATACLASS_HSUPA,
	    "3g" },
	{ MBIM_DATACLASS_LTE, "4g" },
	{ 0, NULL }
};

static int
umb_descr2val(const struct umb_valdescr *vdp, char *str)
{
	while (vdp->descr != NULL) {
		if (!strcasecmp(vdp->descr, str))
			return vdp->val;
		vdp++;
	}
	return 0;
}

void
umb_status(void)
{
	struct umb_info mi;
	char	 provider[UMB_PROVIDERNAME_MAXLEN+1];
	char	 providerid[UMB_PROVIDERID_MAXLEN+1];
	char	 roamingtxt[UMB_ROAMINGTEXT_MAXLEN+1];
	char	 devid[UMB_DEVID_MAXLEN+1];
	char	 fwinfo[UMB_FWINFO_MAXLEN+1];
	char	 hwinfo[UMB_HWINFO_MAXLEN+1];
	char	 sid[UMB_SUBSCRIBERID_MAXLEN+1];
	char	 iccid[UMB_ICCID_MAXLEN+1];
	char	 apn[UMB_APN_MAXLEN+1];
	char	 pn[UMB_PHONENR_MAXLEN+1];
	int	 i, n;
	char	 astr[INET6_ADDRSTRLEN];

	memset((char *)&mi, 0, sizeof(mi));
	ifr.ifr_data = (caddr_t)&mi;
	if (ioctl(sock, SIOCGUMBINFO, (caddr_t)&ifr) == -1)
		return;

	if (mi.nwerror) {
		/* 3GPP 24.008 Cause Code */
		printf("\terror: ");
		switch (mi.nwerror) {
		case 2:
			printf("SIM not activated");
			break;
		case 4:
			printf("Roaming not supported");
			break;
		case 6:
			printf("SIM reported stolen");
			break;
		case 7:
			printf("No GPRS subscription");
			break;
		case 8:
			printf("GPRS and non-GPRS services not allowed");
			break;
		case 11:
			printf("Subscription expired");
			break;
		case 12:
			printf("Subscription does not cover current location");
			break;
		case 13:
			printf("No roaming in this location");
			break;
		case 14:
			printf("GPRS not supported");
			break;
		case 15:
			printf("No subscription for the service");
			break;
		case 17:
			printf("Registration failed");
			break;
		case 22:
			printf("Network congestion");
			break;
		default:
			printf("Error code %d", mi.nwerror);
			break;
		}
		printf("\n");
	}

	printf("\troaming %s registration %s",
	    mi.enable_roaming ? "enabled" : "disabled",
	    umb_val2descr(umb_regstate, mi.regstate));
	utf16_to_char(mi.roamingtxt, UMB_ROAMINGTEXT_MAXLEN,
	    roamingtxt, sizeof (roamingtxt));
	if (roamingtxt[0])
		printf(" [%s]", roamingtxt);
	printf("\n");

	if (showclasses)
		umb_printclasses("available classes", mi.supportedclasses);
	printf("\tstate %s cell-class %s",
	    umb_val2descr(umb_istate, mi.state),
	    umb_val2descr(umb_dataclass, mi.highestclass));
	if (mi.rssi != UMB_VALUE_UNKNOWN && mi.rssi != 0)
		printf(" rssi %ddBm", mi.rssi);
	if (mi.uplink_speed != 0 || mi.downlink_speed != 0) {
		char s[2][FMT_SCALED_STRSIZE];
		if (fmt_scaled(mi.uplink_speed, s[0]) != 0)
			snprintf(s[0], sizeof (s[0]), "%llu", mi.uplink_speed);
		if (fmt_scaled(mi.downlink_speed, s[1]) != 0)
			snprintf(s[1], sizeof (s[1]), "%llu", mi.downlink_speed);
		printf(" speed %sbps up %sbps down", s[0], s[1]);
	}
	printf("\n");

	printf("\tSIM %s PIN ", umb_val2descr(umb_simstate, mi.sim_state));
	switch (mi.pin_state) {
	case UMB_PIN_REQUIRED:
		printf("required");
		break;
	case UMB_PIN_UNLOCKED:
		printf("valid");
		break;
	case UMB_PUK_REQUIRED:
		printf("locked (PUK required)");
		break;
	default:
		printf("unknown state (%d)", mi.pin_state);
		break;
	}
	if (mi.pin_attempts_left != UMB_VALUE_UNKNOWN)
		printf(" (%d attempts left)", mi.pin_attempts_left);
	printf("\n");

	utf16_to_char(mi.sid, UMB_SUBSCRIBERID_MAXLEN, sid, sizeof (sid));
	utf16_to_char(mi.iccid, UMB_ICCID_MAXLEN, iccid, sizeof (iccid));
	utf16_to_char(mi.provider, UMB_PROVIDERNAME_MAXLEN,
	    provider, sizeof (provider));
	utf16_to_char(mi.providerid, UMB_PROVIDERID_MAXLEN,
	    providerid, sizeof (providerid));
	if (sid[0] || iccid[0]) {
		printf("\t");
		n = 0;
		if (sid[0])
			printf("%ssubscriber-id %s", n++ ? " " : "", sid);
		if (iccid[0])
			printf("%sICC-id %s", n++ ? " " : "", iccid);
		printf("\n");
	}

	utf16_to_char(mi.hwinfo, UMB_HWINFO_MAXLEN, hwinfo, sizeof (hwinfo));
	utf16_to_char(mi.devid, UMB_DEVID_MAXLEN, devid, sizeof (devid));
	utf16_to_char(mi.fwinfo, UMB_FWINFO_MAXLEN, fwinfo, sizeof (fwinfo));
	if (hwinfo[0] || devid[0] || fwinfo[0]) {
		printf("\t");
		n = 0;
		if (hwinfo[0])
			printf("%sdevice %s", n++ ? " " : "", hwinfo);
		if (devid[0]) {
			printf("%s", n++ ? " " : "");
			switch (mi.cellclass) {
			case MBIM_CELLCLASS_GSM:
				printf("IMEI");
				break;
			case MBIM_CELLCLASS_CDMA:
				n = strlen(devid);
				if (n == 8 || n == 11) {
					printf("ESN");
					break;
				} else if (n == 14 || n == 18) {
					printf("MEID");
					break;
				}
				/*FALLTHROUGH*/
			default:
				printf("ID");
				break;
			}
			printf(" %s", devid);
		}
		if (fwinfo[0])
			printf("%sfirmware %s", n++ ? " " : "", fwinfo);
		printf("\n");
	}

	utf16_to_char(mi.pn, UMB_PHONENR_MAXLEN, pn, sizeof (pn));
	utf16_to_char(mi.apn, UMB_APN_MAXLEN, apn, sizeof (apn));
	if (pn[0] || apn[0] || provider[0] || providerid[0]) {
		printf("\t");
		n = 0;
		if (pn[0])
			printf("%sphone# %s", n++ ? " " : "", pn);
		if (apn[0])
			printf("%sAPN %s", n++ ? " " : "", apn);
		if (provider[0])
			printf("%sprovider %s", n++ ? " " : "", provider);
		if (providerid[0])
			printf("%sprovider-id %s", n ? " " : "", providerid);
		printf("\n");
	}

	for (i = 0, n = 0; i < UMB_MAX_DNSSRV; i++) {
		if (mi.ipv4dns[i].s_addr == INADDR_ANY)
			break;
		printf("%s %s", n++ ? "" : "\tdns",
		    inet_ntop(AF_INET, &mi.ipv4dns[i], astr, sizeof(astr)));
	}
	for (i = 0; i < UMB_MAX_DNSSRV; i++) {
		if (memcmp(&mi.ipv6dns[i], &in6addr_any,
		    sizeof (mi.ipv6dns[i])) == 0)
			break;
		printf("%s %s", n++ ? "" : "\tdns",
		    inet_ntop(AF_INET6, &mi.ipv6dns[i], astr, sizeof(astr)));
	}
	if (n)
		printf("\n");
}

void
umb_printclasses(char *tag, int c)
{
	int	 i;
	char	*sep = "";

	printf("\t%s: ", tag);
	i = 0;
	while (umb_dataclass[i].descr) {
		if (umb_dataclass[i].val & c) {
			printf("%s%s", sep, umb_dataclass[i].descr);
			sep = ",";
		}
		i++;
	}
	printf("\n");
}

int
umb_parse_classes(const char *spec)
{
	char	*optlist, *str;
	int	 c = 0, v;

	if ((optlist = strdup(spec)) == NULL)
		err(1, "strdup");
	str = strtok(optlist, ",");
	while (str != NULL) {
		if ((v = umb_descr2val(umb_dataclass, str)) != 0 ||
		    (v = umb_descr2val(umb_classalias, str)) != 0)
			c |= v;
		str = strtok(NULL, ",");
	}
	free(optlist);
	return c;
}

void
umb_setpin(const char *pin, int d)
{
	umb_pinop(MBIM_PIN_OP_ENTER, 0, pin, NULL);
}

void
umb_chgpin(const char *pin, const char *newpin)
{
	umb_pinop(MBIM_PIN_OP_CHANGE, 0, pin, newpin);
}

void
umb_puk(const char *pin, const char *newpin)
{
	umb_pinop(MBIM_PIN_OP_ENTER, 1, pin, newpin);
}

void
umb_pinop(int op, int is_puk, const char *pin, const char *newpin)
{
	struct umb_parameter mp;

	memset(&mp, 0, sizeof (mp));
	ifr.ifr_data = (caddr_t)&mp;
	if (ioctl(sock, SIOCGUMBPARAM, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCGUMBPARAM", ifr.ifr_name);

	mp.op = op;
	mp.is_puk = is_puk;
	if ((mp.pinlen = char_to_utf16(pin, (uint16_t *)mp.pin,
	    sizeof (mp.pin))) == -1)
		errx(1, "PIN too long");

	if (newpin) {
		if ((mp.newpinlen = char_to_utf16(newpin, (uint16_t *)mp.newpin,
		    sizeof (mp.newpin))) == -1)
			errx(1, "new PIN too long");
	}

	if (ioctl(sock, SIOCSUMBPARAM, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCSUMBPARAM", ifr.ifr_name);
}

void
umb_apn(const char *apn, int d)
{
	struct umb_parameter mp;

	memset(&mp, 0, sizeof (mp));
	ifr.ifr_data = (caddr_t)&mp;
	if (ioctl(sock, SIOCGUMBPARAM, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCGUMBPARAM", ifr.ifr_name);

	if (d != 0)
		memset(mp.apn, 0, sizeof (mp.apn));
	else if ((mp.apnlen = char_to_utf16(apn, mp.apn,
	    sizeof (mp.apn))) == -1)
		errx(1, "APN too long");

	if (ioctl(sock, SIOCSUMBPARAM, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCSUMBPARAM", ifr.ifr_name);
}

void
umb_setclass(const char *val, int d)
{
	struct umb_parameter mp;

	if (val == NULL) {
		if (showclasses)
			usage();
		showclasses = 1;
		return;
	}

	memset(&mp, 0, sizeof (mp));
	ifr.ifr_data = (caddr_t)&mp;
	if (ioctl(sock, SIOCGUMBPARAM, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCGUMBPARAM", ifr.ifr_name);
	if (d != -1)
		mp.preferredclasses = umb_parse_classes(val);
	else
		mp.preferredclasses = MBIM_DATACLASS_NONE;
	if (ioctl(sock, SIOCSUMBPARAM, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCSUMBPARAM", ifr.ifr_name);
}

void
umb_roaming(const char *val, int d)
{
	struct umb_parameter mp;

	memset(&mp, 0, sizeof (mp));
	ifr.ifr_data = (caddr_t)&mp;
	if (ioctl(sock, SIOCGUMBPARAM, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCGUMBPARAM", ifr.ifr_name);
	mp.roaming = d;
	if (ioctl(sock, SIOCSUMBPARAM, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCSUMBPARAM", ifr.ifr_name);
}

void
utf16_to_char(uint16_t *in, int inlen, char *out, size_t outlen)
{
	uint16_t c;

	while (outlen > 0) {
		c = inlen > 0 ? letoh16(*in) : 0;
		if (c == 0 || --outlen == 0) {
			/* always NUL terminate result */
			*out = '\0';
			break;
		}
		*out++ = isascii(c) ? (char)c : '?';
		in++;
		inlen--;
	}
}

int
char_to_utf16(const char *in, uint16_t *out, size_t outlen)
{
	int	 n = 0;
	uint16_t c;

	for (;;) {
		c = *in++;

		if (c == '\0') {
			/*
			 * NUL termination is not required, but zero out the
			 * residual buffer
			 */
			memset(out, 0, outlen);
			return n;
		}
		if (outlen < sizeof (*out))
			return -1;

		*out++ = htole16(c);
		n += sizeof (*out);
		outlen -= sizeof (*out);
	}
}

#endif

#define SIN(x) ((struct sockaddr_in *) &(x))
struct sockaddr_in *sintab[] = {
SIN(ridreq.ifr_addr), SIN(in_addreq.ifra_addr),
SIN(in_addreq.ifra_mask), SIN(in_addreq.ifra_broadaddr)};

void
in_getaddr(const char *s, int which)
{
	struct sockaddr_in *sin = sintab[which], tsin;
	struct hostent *hp;
	int bits, l;
	char p[3];

	bzero(&tsin, sizeof(tsin));
	sin->sin_len = sizeof(*sin);
	if (which != MASK)
		sin->sin_family = AF_INET;

	if (which == ADDR && strrchr(s, '/') != NULL &&
	    (bits = inet_net_pton(AF_INET, s, &tsin.sin_addr,
	    sizeof(tsin.sin_addr))) != -1) {
		l = snprintf(p, sizeof(p), "%d", bits);
		if (l < 0 || l >= sizeof(p))
			errx(1, "%d: bad prefixlen", bits);
		in_getprefix(p, MASK);
		memcpy(&sin->sin_addr, &tsin.sin_addr, sizeof(sin->sin_addr));
	} else if (inet_aton(s, &sin->sin_addr) == 0) {
		if ((hp = gethostbyname(s)))
			memcpy(&sin->sin_addr, hp->h_addr, hp->h_length);
		else
			errx(1, "%s: bad value", s);
	}
	if (which == MASK && (ntohl(sin->sin_addr.s_addr) &
	    (~ntohl(sin->sin_addr.s_addr) >> 1)))
		errx(1, "%s: non-contiguous mask", s);
}

void
in_getprefix(const char *plen, int which)
{
	struct sockaddr_in *sin = sintab[which];
	const char *errmsg = NULL;
	u_char *cp;
	int len;

	len = strtonum(plen, 0, 32, &errmsg);
	if (errmsg)
		errx(1, "prefix %s: %s", plen, errmsg);

	sin->sin_len = sizeof(*sin);
	if (which != MASK)
		sin->sin_family = AF_INET;
	if ((len == 0) || (len == 32)) {
		memset(&sin->sin_addr, 0xff, sizeof(struct in_addr));
		return;
	}
	memset((void *)&sin->sin_addr, 0x00, sizeof(sin->sin_addr));
	for (cp = (u_char *)&sin->sin_addr; len > 7; len -= 8)
		*cp++ = 0xff;
	if (len)
		*cp = 0xff << (8 - len);
}

/*
 * Print a value a la the %b format of the kernel's printf
 */
void
printb(char *s, unsigned int v, unsigned char *bits)
{
	int i, any = 0;
	unsigned char c;

	if (bits && *bits == 8)
		printf("%s=%o", s, v);
	else
		printf("%s=%x", s, v);

	if (bits) {
		bits++;
		putchar('<');
		while ((i = *bits++)) {
			if (v & (1 << (i-1))) {
				if (any)
					putchar(',');
				any = 1;
				for (; (c = *bits) > 32; bits++)
					putchar(c);
			} else
				for (; *bits > 32; bits++)
					;
		}
		putchar('>');
	}
}

/*
 * A simple version of printb for status output
 */
void
printb_status(unsigned short v, unsigned char *bits)
{
	int i, any = 0;
	unsigned char c;

	if (bits) {
		bits++;
		while ((i = *bits++)) {
			if (v & (1 << (i-1))) {
				if (any)
					putchar(',');
				any = 1;
				for (; (c = *bits) > 32; bits++)
					putchar(tolower(c));
			} else
				for (; *bits > 32; bits++)
					;
		}
	}
}

#define SIN6(x) ((struct sockaddr_in6 *) &(x))
struct sockaddr_in6 *sin6tab[] = {
SIN6(in6_ridreq.ifr_addr), SIN6(in6_addreq.ifra_addr),
SIN6(in6_addreq.ifra_prefixmask), SIN6(in6_addreq.ifra_dstaddr)};

void
in6_getaddr(const char *s, int which)
{
	struct sockaddr_in6 *sin6 = sin6tab[which];
	struct addrinfo hints, *res;
	char buf[HOST_NAME_MAX+1 + sizeof("/128")], *pfxlen;
	int error;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_DGRAM;	/*dummy*/

	if (which == ADDR && strchr(s, '/') != NULL) {
		if (strlcpy(buf, s, sizeof(buf)) >= sizeof(buf))
			errx(1, "%s: bad value", s);
		pfxlen = strchr(buf, '/');
		*pfxlen++ = '\0';
		s = buf;
		in6_getprefix(pfxlen, MASK);
		explicit_prefix = 1;
	}

	error = getaddrinfo(s, "0", &hints, &res);
	if (error)
		errx(1, "%s: %s", s, gai_strerror(error));
	memcpy(sin6, res->ai_addr, res->ai_addrlen);
#ifdef __KAME__
	if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr) &&
	    *(u_int16_t *)&sin6->sin6_addr.s6_addr[2] == 0 &&
	    sin6->sin6_scope_id) {
		*(u_int16_t *)&sin6->sin6_addr.s6_addr[2] =
		    htons(sin6->sin6_scope_id & 0xffff);
		sin6->sin6_scope_id = 0;
	}
#endif /* __KAME__ */
	freeaddrinfo(res);
}

void
in6_getprefix(const char *plen, int which)
{
	struct sockaddr_in6 *sin6 = sin6tab[which];
	const char *errmsg = NULL;
	u_char *cp;
	int len;

	len = strtonum(plen, 0, 128, &errmsg);
	if (errmsg)
		errx(1, "prefix %s: %s", plen, errmsg);

	sin6->sin6_len = sizeof(*sin6);
	if (which != MASK)
		sin6->sin6_family = AF_INET6;
	if ((len == 0) || (len == 128)) {
		memset(&sin6->sin6_addr, 0xff, sizeof(struct in6_addr));
		return;
	}
	memset((void *)&sin6->sin6_addr, 0x00, sizeof(sin6->sin6_addr));
	for (cp = (u_char *)&sin6->sin6_addr; len > 7; len -= 8)
		*cp++ = 0xff;
	if (len)
		*cp = 0xff << (8 - len);
}

int
prefix(void *val, int size)
{
	u_char *nam = (u_char *)val;
	int byte, bit, plen = 0;

	for (byte = 0; byte < size; byte++, plen += 8)
		if (nam[byte] != 0xff)
			break;
	if (byte == size)
		return (plen);
	for (bit = 7; bit != 0; bit--, plen++)
		if (!(nam[byte] & (1 << bit)))
			break;
	for (; bit != 0; bit--)
		if (nam[byte] & (1 << bit))
			return (0);
	byte++;
	for (; byte < size; byte++)
		if (nam[byte])
			return (0);
	return (plen);
}

/* Print usage and exit  */
__dead void
usage(void)
{
	fprintf(stderr,
	    "usage: ifconfig [-AaC] [-M lladdr] [interface] [address_family]\n"
	    "\t\t[address [dest_address]] [parameters]\n");
	exit(1);
}

void
getifgroups(void)
{
	int			 len, cnt;
	struct ifgroupreq	 ifgr;
	struct ifg_req		*ifg;

	memset(&ifgr, 0, sizeof(ifgr));
	strlcpy(ifgr.ifgr_name, ifname, IFNAMSIZ);

	if (ioctl(sock, SIOCGIFGROUP, (caddr_t)&ifgr) == -1) {
		if (errno == EINVAL || errno == ENOTTY)
			return;
		else
			err(1, "%s: SIOCGIFGROUP", ifgr.ifgr_name);
	}

	len = ifgr.ifgr_len;
	ifgr.ifgr_groups = calloc(len / sizeof(struct ifg_req),
	    sizeof(struct ifg_req));
	if (ifgr.ifgr_groups == NULL)
		err(1, "getifgroups");
	if (ioctl(sock, SIOCGIFGROUP, (caddr_t)&ifgr) == -1)
		err(1, "%s: SIOCGIFGROUP", ifgr.ifgr_name);

	cnt = 0;
	ifg = ifgr.ifgr_groups;
	for (; ifg && len >= sizeof(struct ifg_req); ifg++) {
		len -= sizeof(struct ifg_req);
		if (strcmp(ifg->ifgrq_group, "all")) {
			if (cnt == 0)
				printf("\tgroups:");
			cnt++;
			printf(" %s", ifg->ifgrq_group);
		}
	}
	if (cnt)
		printf("\n");

	free(ifgr.ifgr_groups);
}

#ifndef SMALL
void
printifhwfeatures(const char *unused, int show)
{
	struct if_data ifrdat;

	if (!show) {
		if (showcapsflag)
			usage();
		showcapsflag = 1;
		return;
	}
	bzero(&ifrdat, sizeof(ifrdat));
	ifr.ifr_data = (caddr_t)&ifrdat;
	if (ioctl(sock, SIOCGIFDATA, (caddr_t)&ifr) == -1)
		err(1, "%s: SIOCGIFDATA", ifr.ifr_name);
	printb("\thwfeatures", (u_int)ifrdat.ifi_capabilities, HWFEATURESBITS);

	if (ioctl(sock, SIOCGIFHARDMTU, (caddr_t)&ifr) != -1) {
		if (ifr.ifr_hardmtu)
			printf(" hardmtu %u", ifr.ifr_hardmtu);
	}
	putchar('\n');
}
#endif

char *
sec2str(time_t total)
{
	static char result[256];
	char *p = result;
	char *end = &result[sizeof(result)];

	snprintf(p, end - p, "%lld", (long long)total);
	return (result);
}

void
setiflladdr(const char *addr, int param)
{
	struct ether_addr *eap, eabuf;

	if (!strcmp(addr, "random")) {
		arc4random_buf(&eabuf, sizeof eabuf);
		/* Non-multicast and claim it is a hardware address */
		eabuf.ether_addr_octet[0] &= 0xfc;
		eap = &eabuf;
	} else {
		eap = ether_aton(addr);
		if (eap == NULL) {
			warnx("malformed link-level address");
			return;
		}
	}
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	ifr.ifr_addr.sa_len = ETHER_ADDR_LEN;
	ifr.ifr_addr.sa_family = AF_LINK;
	bcopy(eap, ifr.ifr_addr.sa_data, ETHER_ADDR_LEN);
	if (ioctl(sock, SIOCSIFLLADDR, (caddr_t)&ifr) == -1)
		warn("SIOCSIFLLADDR");
}

#ifndef SMALL
void
setrdomain(const char *id, int param)
{
	const char *errmsg = NULL;
	int rdomainid;

	rdomainid = strtonum(id, 0, RT_TABLEID_MAX, &errmsg);
	if (errmsg)
		errx(1, "rdomain %s: %s", id, errmsg);

	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	ifr.ifr_rdomainid = rdomainid;
	if (ioctl(sock, SIOCSIFRDOMAIN, (caddr_t)&ifr) == -1)
		warn("SIOCSIFRDOMAIN");
}

void
unsetrdomain(const char *ignored, int alsoignored)
{
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	ifr.ifr_rdomainid = 0;
	if (ioctl(sock, SIOCSIFRDOMAIN, (caddr_t)&ifr) == -1)
		warn("SIOCSIFRDOMAIN");
}
#endif

#ifndef SMALL
void
setpair(const char *val, int d)
{
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	if ((ifr.ifr_index = if_nametoindex(val)) == 0) {
		errno = ENOENT;
		err(1, "patch %s", val);
	}
	if (ioctl(sock, SIOCSIFPAIR, (caddr_t)&ifr) == -1)
		warn("SIOCSIFPAIR");
}

void
unsetpair(const char *val, int d)
{
	ifr.ifr_index = 0;
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	if (ioctl(sock, SIOCSIFPAIR, (caddr_t)&ifr) == -1)
		warn("SIOCSIFPAIR");
}
#endif

#ifdef SMALL
void
setignore(const char *id, int param)
{
	/* just digest the command */
}
#endif

int
findmac(const char *mac)
{
	struct ifaddrs *ifap, *ifa;
	const char *ifnam = NULL;
	struct if_clonereq *ifcr;
	int ret = 0;

	ifcr = get_cloners();
	if (getifaddrs(&ifap) != 0)
		err(1, "getifaddrs");

	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		struct sockaddr_dl *sdl = (struct sockaddr_dl *)ifa->ifa_addr;

		if (sdl != NULL && sdl->sdl_alen &&
		    (sdl->sdl_type == IFT_ETHER || sdl->sdl_type == IFT_CARP)) {
			if (strcmp(ether_ntoa((struct ether_addr *)LLADDR(sdl)),
			    mac) == 0) {
				char *cp, *nam = ifa->ifa_name;
				int idx, skip = 0;
				size_t len;

				/* MACs on cloned devices are ignored */
				for (len = 0; nam[len]; len++)
					if (isdigit((unsigned char)nam[len]))
						break;
				for (cp = ifcr->ifcr_buffer, idx = 0;
				    idx < ifcr->ifcr_count;
				    idx++, cp += IFNAMSIZ) {
					if (strncmp(nam, cp, len) == 0) {
						skip = 1;
						break;
					}
				}
				if (skip)
					continue;

				if (ifnam) {	/* same MAC on multiple ifp */
					ret = 1;
					goto done;
				}
				ifnam = nam;
			}
		}
	}
	if (ifnam)
		printf("%s\n", ifnam);
done:
	free(ifcr->ifcr_buffer);
	freeifaddrs(ifap);
	return ret;
}
