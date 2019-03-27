/*	$NetBSD: bootp.c,v 1.14 1998/02/16 11:10:54 drochner Exp $	*/

/*
 * Copyright (c) 1992 Regents of the University of California.
 * All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
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
 *
 * @(#) Header: bootp.c,v 1.4 93/09/11 03:13:51 leres Exp  (LBL)
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stddef.h>
#include <sys/types.h>
#include <sys/limits.h>
#include <sys/endian.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>

#include <string.h>

#define BOOTP_DEBUGxx
#define SUPPORT_DHCP

#define	DHCP_ENV_NOVENDOR	1	/* do not parse vendor options */
#define	DHCP_ENV_PXE		10	/* assume pxe vendor options */
#define	DHCP_ENV_FREEBSD	11	/* assume freebsd vendor options */
/* set DHCP_ENV to one of the values above to export dhcp options to kenv */
#define DHCP_ENV		DHCP_ENV_NO_VENDOR

#include "stand.h"
#include "net.h"
#include "netif.h"
#include "bootp.h"


struct in_addr servip;

static time_t	bot;

static	char vm_rfc1048[4] = VM_RFC1048;
#ifdef BOOTP_VEND_CMU
static	char vm_cmu[4] = VM_CMU;
#endif

/* Local forwards */
static	ssize_t bootpsend(struct iodesc *, void *, size_t);
static	ssize_t bootprecv(struct iodesc *, void **, void **, time_t, void *);
static	int vend_rfc1048(u_char *, u_int);
#ifdef BOOTP_VEND_CMU
static	void vend_cmu(u_char *);
#endif

#ifdef DHCP_ENV		/* export the dhcp response to kenv */
struct dhcp_opt;
static void setenv_(u_char *cp,  u_char *ep, struct dhcp_opt *opts);
#else
#define setenv_(a, b, c)
#endif

#ifdef SUPPORT_DHCP
static char expected_dhcpmsgtype = -1, dhcp_ok;
struct in_addr dhcp_serverip;
#endif
struct bootp *bootp_response;
size_t bootp_response_size;

static void
bootp_fill_request(unsigned char *bp_vend)
{
	/*
	 * We are booting from PXE, we want to send the string
	 * 'PXEClient' to the DHCP server so you have the option of
	 * only responding to PXE aware dhcp requests.
	 */
	bp_vend[0] = TAG_CLASSID;
	bp_vend[1] = 9;
	bcopy("PXEClient", &bp_vend[2], 9);
	bp_vend[11] = TAG_USER_CLASS;
	/* len of each user class + number of user class */
	bp_vend[12] = 8;
	/* len of the first user class */
	bp_vend[13] = 7;
	bcopy("FreeBSD", &bp_vend[14], 7);
	bp_vend[21] = TAG_PARAM_REQ;
	bp_vend[22] = 7;
	bp_vend[23] = TAG_ROOTPATH;
	bp_vend[24] = TAG_HOSTNAME;
	bp_vend[25] = TAG_SWAPSERVER;
	bp_vend[26] = TAG_GATEWAY;
	bp_vend[27] = TAG_SUBNET_MASK;
	bp_vend[28] = TAG_INTF_MTU;
	bp_vend[29] = TAG_SERVERID;
	bp_vend[30] = TAG_END;
}

/* Fetch required bootp infomation */
void
bootp(int sock)
{
	void *pkt;
	struct iodesc *d;
	struct bootp *bp;
	struct {
		u_char header[HEADER_SIZE];
		struct bootp wbootp;
	} wbuf;
	struct bootp *rbootp;

#ifdef BOOTP_DEBUG
 	if (debug)
		printf("bootp: socket=%d\n", sock);
#endif
	if (!bot)
		bot = getsecs();
	
	if (!(d = socktodesc(sock))) {
		printf("bootp: bad socket. %d\n", sock);
		return;
	}
#ifdef BOOTP_DEBUG
 	if (debug)
		printf("bootp: d=%lx\n", (long)d);
#endif

	bp = &wbuf.wbootp;
	bzero(bp, sizeof(*bp));

	bp->bp_op = BOOTREQUEST;
	bp->bp_htype = 1;		/* 10Mb Ethernet (48 bits) */
	bp->bp_hlen = 6;
	bp->bp_xid = htonl(d->xid);
	MACPY(d->myea, bp->bp_chaddr);
	strncpy(bp->bp_file, bootfile, sizeof(bp->bp_file));
	bcopy(vm_rfc1048, bp->bp_vend, sizeof(vm_rfc1048));
#ifdef SUPPORT_DHCP
	bp->bp_vend[4] = TAG_DHCP_MSGTYPE;
	bp->bp_vend[5] = 1;
	bp->bp_vend[6] = DHCPDISCOVER;
	bootp_fill_request(&bp->bp_vend[7]);

#else
	bp->bp_vend[4] = TAG_END;
#endif

	d->myip.s_addr = INADDR_ANY;
	d->myport = htons(IPPORT_BOOTPC);
	d->destip.s_addr = INADDR_BROADCAST;
	d->destport = htons(IPPORT_BOOTPS);

#ifdef SUPPORT_DHCP
	expected_dhcpmsgtype = DHCPOFFER;
	dhcp_ok = 0;
#endif

	if(sendrecv(d,
		    bootpsend, bp, sizeof(*bp),
		    bootprecv, &pkt, (void **)&rbootp, NULL) == -1) {
	    printf("bootp: no reply\n");
	    return;
	}

#ifdef SUPPORT_DHCP
	if(dhcp_ok) {
		uint32_t leasetime;
		bp->bp_vend[6] = DHCPREQUEST;
		bp->bp_vend[7] = TAG_REQ_ADDR;
		bp->bp_vend[8] = 4;
		bcopy(&rbootp->bp_yiaddr, &bp->bp_vend[9], 4);
		bp->bp_vend[13] = TAG_SERVERID;
		bp->bp_vend[14] = 4;
		bcopy(&dhcp_serverip.s_addr, &bp->bp_vend[15], 4);
		bp->bp_vend[19] = TAG_LEASETIME;
		bp->bp_vend[20] = 4;
		leasetime = htonl(300);
		bcopy(&leasetime, &bp->bp_vend[21], 4);
		bootp_fill_request(&bp->bp_vend[25]);

		expected_dhcpmsgtype = DHCPACK;

		free(pkt);
		if(sendrecv(d,
			    bootpsend, bp, sizeof(*bp),
			    bootprecv, &pkt, (void **)&rbootp, NULL) == -1) {
			printf("DHCPREQUEST failed\n");
			return;
		}
	}
#endif

	myip = d->myip = rbootp->bp_yiaddr;
	servip = rbootp->bp_siaddr;
	if (rootip.s_addr == INADDR_ANY)
		rootip = servip;
	bcopy(rbootp->bp_file, bootfile, sizeof(bootfile));
	bootfile[sizeof(bootfile) - 1] = '\0';

	if (!netmask) {
		if (IN_CLASSA(ntohl(myip.s_addr)))
			netmask = htonl(IN_CLASSA_NET);
		else if (IN_CLASSB(ntohl(myip.s_addr)))
			netmask = htonl(IN_CLASSB_NET);
		else
			netmask = htonl(IN_CLASSC_NET);
#ifdef BOOTP_DEBUG
		if (debug)
			printf("'native netmask' is %s\n", intoa(netmask));
#endif
	}

#ifdef BOOTP_DEBUG
	if (debug)
		printf("mask: %s\n", intoa(netmask));
#endif

	/* We need a gateway if root is on a different net */
	if (!SAMENET(myip, rootip, netmask)) {
#ifdef BOOTP_DEBUG
		if (debug)
			printf("need gateway for root ip\n");
#endif
	}

	/* Toss gateway if on a different net */
	if (!SAMENET(myip, gateip, netmask)) {
#ifdef BOOTP_DEBUG
		if (debug)
			printf("gateway ip (%s) bad\n", inet_ntoa(gateip));
#endif
		gateip.s_addr = 0;
	}

	/* Bump xid so next request will be unique. */
	++d->xid;
	free(pkt);
}

/* Transmit a bootp request */
static ssize_t
bootpsend(struct iodesc *d, void *pkt, size_t len)
{
	struct bootp *bp;

#ifdef BOOTP_DEBUG
	if (debug)
		printf("bootpsend: d=%lx called.\n", (long)d);
#endif

	bp = pkt;
	bp->bp_secs = htons((u_short)(getsecs() - bot));

#ifdef BOOTP_DEBUG
	if (debug)
		printf("bootpsend: calling sendudp\n");
#endif

	return (sendudp(d, pkt, len));
}

static ssize_t
bootprecv(struct iodesc *d, void **pkt, void **payload, time_t tleft,
    void *extra)
{
	ssize_t n;
	struct bootp *bp;
	void *ptr;

#ifdef BOOTP_DEBUG
	if (debug)
		printf("bootp_recvoffer: called\n");
#endif

	ptr = NULL;
	n = readudp(d, &ptr, (void **)&bp, tleft);
	if (n == -1 || n < sizeof(struct bootp) - BOOTP_VENDSIZE)
		goto bad;

#ifdef BOOTP_DEBUG
	if (debug)
		printf("bootprecv: checked.  bp = %p, n = %zd\n", bp, n);
#endif
	if (bp->bp_xid != htonl(d->xid)) {
#ifdef BOOTP_DEBUG
		if (debug) {
			printf("bootprecv: expected xid 0x%lx, got 0x%x\n",
			    d->xid, ntohl(bp->bp_xid));
		}
#endif
		goto bad;
	}

#ifdef BOOTP_DEBUG
	if (debug)
		printf("bootprecv: got one!\n");
#endif

	/* Suck out vendor info */
	if (bcmp(vm_rfc1048, bp->bp_vend, sizeof(vm_rfc1048)) == 0) {
		int vsize = n - offsetof(struct bootp, bp_vend);
		if (vend_rfc1048(bp->bp_vend, vsize) != 0)
		    goto bad;

		/* Save copy of bootp reply or DHCP ACK message */
		if (bp->bp_op == BOOTREPLY &&
		    ((dhcp_ok == 1 && expected_dhcpmsgtype == DHCPACK) ||
		    dhcp_ok == 0)) {
			free(bootp_response);
			bootp_response = malloc(n);
			if (bootp_response != NULL) {
				bootp_response_size = n;
				bcopy(bp, bootp_response, bootp_response_size);
			}
		}
	}
#ifdef BOOTP_VEND_CMU
	else if (bcmp(vm_cmu, bp->bp_vend, sizeof(vm_cmu)) == 0)
		vend_cmu(bp->bp_vend);
#endif
	else
		printf("bootprecv: unknown vendor 0x%lx\n", (long)bp->bp_vend);

	*pkt = ptr;
	*payload = bp;
	return (n);
bad:
	free(ptr);
	errno = 0;
	return (-1);
}

static int
vend_rfc1048(u_char *cp, u_int len)
{
	u_char *ep;
	int size;
	u_char tag;
	const char *val;

#ifdef BOOTP_DEBUG
	if (debug)
		printf("vend_rfc1048 bootp info. len=%d\n", len);
#endif
	ep = cp + len;

	/* Step over magic cookie */
	cp += sizeof(int);

	setenv_(cp, ep, NULL);

	while (cp < ep) {
		tag = *cp++;
		size = *cp++;
		if (tag == TAG_END)
			break;

		if (tag == TAG_SUBNET_MASK) {
			bcopy(cp, &netmask, sizeof(netmask));
		}
		if (tag == TAG_GATEWAY) {
			bcopy(cp, &gateip.s_addr, sizeof(gateip.s_addr));
		}
		if (tag == TAG_SWAPSERVER) {
			/* let it override bp_siaddr */
			bcopy(cp, &rootip.s_addr, sizeof(rootip.s_addr));
		}
		if (tag == TAG_ROOTPATH) {
			if ((val = getenv("dhcp.root-path")) == NULL)
				val = (const char *)cp;
			strlcpy(rootpath, val, sizeof(rootpath));
		}
		if (tag == TAG_HOSTNAME) {
			if ((val = getenv("dhcp.host-name")) == NULL)
				val = (const char *)cp;
			strlcpy(hostname, val, sizeof(hostname));
		}
		if (tag == TAG_INTF_MTU) {
			intf_mtu = 0;
			if ((val = getenv("dhcp.interface-mtu")) != NULL) {
				unsigned long tmp;
				char *end;

				errno = 0;
				/*
				 * Do not allow MTU to exceed max IPv4 packet
				 * size, max value of 16-bit word.
				 */
				tmp = strtoul(val, &end, 0);
				if (errno != 0 ||
				    *val == '\0' || *end != '\0' ||
				    tmp > USHRT_MAX) {
					printf("%s: bad value: \"%s\", "
					    "ignoring\n",
					    "dhcp.interface-mtu", val);
				} else {
					intf_mtu = (u_int)tmp;
				}
			}
			if (intf_mtu <= 0)
				intf_mtu = be16dec(cp);
		}
#ifdef SUPPORT_DHCP
		if (tag == TAG_DHCP_MSGTYPE) {
			if(*cp != expected_dhcpmsgtype)
			    return(-1);
			dhcp_ok = 1;
		}
		if (tag == TAG_SERVERID) {
			bcopy(cp, &dhcp_serverip.s_addr,
			      sizeof(dhcp_serverip.s_addr));
		}
#endif
		cp += size;
	}
	return(0);
}

#ifdef BOOTP_VEND_CMU
static void
vend_cmu(u_char *cp)
{
	struct cmu_vend *vp;

#ifdef BOOTP_DEBUG
	if (debug)
		printf("vend_cmu bootp info.\n");
#endif
	vp = (struct cmu_vend *)cp;

	if (vp->v_smask.s_addr != 0) {
		netmask = vp->v_smask.s_addr;
	}
	if (vp->v_dgate.s_addr != 0) {
		gateip = vp->v_dgate;
	}
}
#endif

#ifdef DHCP_ENV
/*
 * Parse DHCP options and store them into kenv variables.
 * Original code from Danny Braniss, modifications by Luigi Rizzo.
 *
 * The parser is driven by tables which specify the type and name of
 * each dhcp option and how it appears in kenv.
 * The first entry in the list contains the prefix used to set the kenv
 * name (including the . if needed), the last entry must have a 0 tag.
 * Entries do not need to be sorted though it helps for readability.
 *
 * Certain vendor-specific tables can be enabled according to DHCP_ENV.
 * Set it to 0 if you don't want any.
 */
enum opt_fmt { __NONE = 0,
	__8 = 1, __16 = 2, __32 = 4,	/* Unsigned fields, value=size	*/
	__IP,				/* IPv4 address			*/
	__TXT,				/* C string			*/
	__BYTES,			/* byte sequence, printed %02x	*/
	__INDIR,			/* name=value			*/
	__ILIST,			/* name=value;name=value ... */
	__VE,				/* vendor specific, recurse	*/
};

struct dhcp_opt {
	uint8_t	tag;
	uint8_t	fmt;
	const char	*desc;
};

static struct dhcp_opt vndr_opt[] = { /* Vendor Specific Options */
#if DHCP_ENV == DHCP_ENV_FREEBSD /* FreeBSD table in the original code */
	{0,	0,	"FreeBSD"},		/* prefix */
	{1,	__TXT,	"kernel"},
	{2,	__TXT,	"kernelname"},
	{3,	__TXT,	"kernel_options"},
	{4,	__IP,	"usr-ip"},
	{5,	__TXT,	"conf-path"},
	{6,	__TXT,	"rc.conf0"},
	{7,	__TXT,	"rc.conf1"},
	{8,	__TXT,	"rc.conf2"},
	{9,	__TXT,	"rc.conf3"},
	{10,	__TXT,	"rc.conf4"},
	{11,	__TXT,	"rc.conf5"},
	{12,	__TXT,	"rc.conf6"},
	{13,	__TXT,	"rc.conf7"},
	{14,	__TXT,	"rc.conf8"},
	{15,	__TXT,	"rc.conf9"},

	{20,	__TXT,  "boot.nfsroot.options"},

	{245,	__INDIR, ""},
	{246,	__INDIR, ""},
	{247,	__INDIR, ""},
	{248,	__INDIR, ""},
	{249,	__INDIR, ""},
	{250,	__INDIR, ""},
	{251,	__INDIR, ""},
	{252,	__INDIR, ""},
	{253,	__INDIR, ""},
	{254,	__INDIR, ""},

#elif DHCP_ENV == DHCP_ENV_PXE		/* some pxe options, RFC4578 */
	{0,	0,	"pxe"},		/* prefix */
	{93,	__16,	"system-architecture"},
	{94,	__BYTES,	"network-interface"},
	{97,	__BYTES,	"machine-identifier"},
#else					/* default (empty) table */
	{0,	0,	"dhcp.vendor."},		/* prefix */
#endif
	{0,	__TXT,	"%soption-%d"}
};

static struct dhcp_opt dhcp_opt[] = {
	/* DHCP Option names, formats and codes, from RFC2132. */
	{0,	0,	"dhcp."},	// prefix
	{1,	__IP,	"subnet-mask"},
	{2,	__32,	"time-offset"}, /* this is signed */
	{3,	__IP,	"routers"},
	{4,	__IP,	"time-servers"},
	{5,	__IP,	"ien116-name-servers"},
	{6,	__IP,	"domain-name-servers"},
	{7,	__IP,	"log-servers"},
	{8,	__IP,	"cookie-servers"},
	{9,	__IP,	"lpr-servers"},
	{10,	__IP,	"impress-servers"},
	{11,	__IP,	"resource-location-servers"},
	{12,	__TXT,	"host-name"},
	{13,	__16,	"boot-size"},
	{14,	__TXT,	"merit-dump"},
	{15,	__TXT,	"domain-name"},
	{16,	__IP,	"swap-server"},
	{17,	__TXT,	"root-path"},
	{18,	__TXT,	"extensions-path"},
	{19,	__8,	"ip-forwarding"},
	{20,	__8,	"non-local-source-routing"},
	{21,	__IP,	"policy-filter"},
	{22,	__16,	"max-dgram-reassembly"},
	{23,	__8,	"default-ip-ttl"},
	{24,	__32,	"path-mtu-aging-timeout"},
	{25,	__16,	"path-mtu-plateau-table"},
	{26,	__16,	"interface-mtu"},
	{27,	__8,	"all-subnets-local"},
	{28,	__IP,	"broadcast-address"},
	{29,	__8,	"perform-mask-discovery"},
	{30,	__8,	"mask-supplier"},
	{31,	__8,	"perform-router-discovery"},
	{32,	__IP,	"router-solicitation-address"},
	{33,	__IP,	"static-routes"},
	{34,	__8,	"trailer-encapsulation"},
	{35,	__32,	"arp-cache-timeout"},
	{36,	__8,	"ieee802-3-encapsulation"},
	{37,	__8,	"default-tcp-ttl"},
	{38,	__32,	"tcp-keepalive-interval"},
	{39,	__8,	"tcp-keepalive-garbage"},
	{40,	__TXT,	"nis-domain"},
	{41,	__IP,	"nis-servers"},
	{42,	__IP,	"ntp-servers"},
	{43,	__VE,	"vendor-encapsulated-options"},
	{44,	__IP,	"netbios-name-servers"},
	{45,	__IP,	"netbios-dd-server"},
	{46,	__8,	"netbios-node-type"},
	{47,	__TXT,	"netbios-scope"},
	{48,	__IP,	"x-font-servers"},
	{49,	__IP,	"x-display-managers"},
	{50,	__IP,	"dhcp-requested-address"},
	{51,	__32,	"dhcp-lease-time"},
	{52,	__8,	"dhcp-option-overload"},
	{53,	__8,	"dhcp-message-type"},
	{54,	__IP,	"dhcp-server-identifier"},
	{55,	__8,	"dhcp-parameter-request-list"},
	{56,	__TXT,	"dhcp-message"},
	{57,	__16,	"dhcp-max-message-size"},
	{58,	__32,	"dhcp-renewal-time"},
	{59,	__32,	"dhcp-rebinding-time"},
	{60,	__TXT,	"vendor-class-identifier"},
	{61,	__TXT,	"dhcp-client-identifier"},
	{64,	__TXT,	"nisplus-domain"},
	{65,	__IP,	"nisplus-servers"},
	{66,	__TXT,	"tftp-server-name"},
	{67,	__TXT,	"bootfile-name"},
	{68,	__IP,	"mobile-ip-home-agent"},
	{69,	__IP,	"smtp-server"},
	{70,	__IP,	"pop-server"},
	{71,	__IP,	"nntp-server"},
	{72,	__IP,	"www-server"},
	{73,	__IP,	"finger-server"},
	{74,	__IP,	"irc-server"},
	{75,	__IP,	"streettalk-server"},
	{76,	__IP,	"streettalk-directory-assistance-server"},
	{77,	__TXT,	"user-class"},
	{85,	__IP,	"nds-servers"},
	{86,	__TXT,	"nds-tree-name"},
	{87,	__TXT,	"nds-context"},
	{210,	__TXT,	"authenticate"},

	/* use the following entries for arbitrary variables */
	{246,	__ILIST, ""},
	{247,	__ILIST, ""},
	{248,	__ILIST, ""},
	{249,	__ILIST, ""},
	{250,	__INDIR, ""},
	{251,	__INDIR, ""},
	{252,	__INDIR, ""},
	{253,	__INDIR, ""},
	{254,	__INDIR, ""},
	{0,	__TXT,	"%soption-%d"}
};

/*
 * parse a dhcp response, set environment variables translating options
 * names and values according to the tables above. Also set dhcp.tags
 * to the list of selected tags.
 */
static void
setenv_(u_char *cp,  u_char *ep, struct dhcp_opt *opts)
{
    u_char	*ncp;
    u_char	tag;
    char	tags[512], *tp;	/* the list of tags */

#define FLD_SEP	','	/* separator in list of elements */
    ncp = cp;
    tp = tags;
    if (opts == NULL)
	opts = dhcp_opt;

    while (ncp < ep) {
	unsigned int	size;		/* option size */
	char *vp, *endv, buf[256];	/* the value buffer */
	struct dhcp_opt *op;

	tag = *ncp++;			/* extract tag and size */
	size = *ncp++;
	cp = ncp;			/* current payload */
	ncp += size;			/* point to the next option */

	if (tag == TAG_END)
	    break;
	if (tag == 0)
	    continue;

	for (op = opts+1; op->tag && op->tag != tag; op++)
		;
	/* if not found we end up on the default entry */

	/*
	 * Copy data into the buffer. libstand does not have snprintf so we
	 * need to be careful with sprintf(). With strings, the source is
	 * always <256 char so shorter than the buffer so we are safe; with
	 * other arguments, the longest string is inet_ntoa which is 16 bytes
	 * so we make sure to have always enough room in the string before
	 * trying an sprint.
	 */
	vp = buf;
	*vp = '\0';
	endv = buf + sizeof(buf) - 1 - 16;	/* last valid write position */

	switch(op->fmt) {
	case __NONE:
	    break;	/* should not happen */

	case __VE: /* recurse, vendor specific */
	    setenv_(cp, cp+size, vndr_opt);
	    break;

	case __IP:	/* ip address */
	    for (; size > 0 && vp < endv; size -= 4, cp += 4) {
		struct	in_addr in_ip;		/* ip addresses */
		if (vp != buf)
		    *vp++ = FLD_SEP;
		bcopy(cp, &in_ip.s_addr, sizeof(in_ip.s_addr));
		sprintf(vp, "%s", inet_ntoa(in_ip));
		vp += strlen(vp);
	    }
	    break;

	case __BYTES:	/* opaque byte string */
	    for (; size > 0 && vp < endv; size -= 1, cp += 1) {
		sprintf(vp, "%02x", *cp);
		vp += strlen(vp);
	    }
	    break;

	case __TXT:
	    bcopy(cp, buf, size);	/* cannot overflow */
	    buf[size] = 0;
	    break;

	case __32:
	case __16:
	case __8:	/* op->fmt is also the length of each field */
	    for (; size > 0 && vp < endv; size -= op->fmt, cp += op->fmt) {
		uint32_t v;
		if (op->fmt == __32)
			v = (cp[0]<<24) + (cp[1]<<16) + (cp[2]<<8) + cp[3];
		else if (op->fmt == __16)
			v = (cp[0]<<8) + cp[1];
		else
			v = cp[0];
		if (vp != buf)
		    *vp++ = FLD_SEP;
		sprintf(vp, "%u", v);
		vp += strlen(vp);
	    }
	    break;

	case __INDIR:	/* name=value */
	case __ILIST:	/* name=value;name=value... */
	    bcopy(cp, buf, size);	/* cannot overflow */
	    buf[size] = '\0';
	    for (endv = buf; endv; endv = vp) {
		char *s = NULL;	/* semicolon ? */

		/* skip leading whitespace */
		while (*endv && strchr(" \t\n\r", *endv))
		    endv++;
		vp = strchr(endv, '=');	/* find name=value separator */
		if (!vp)
		    break;
		*vp++ = 0;
		if (op->fmt == __ILIST && (s = strchr(vp, ';')))
		    *s++ = '\0';
		setenv(endv, vp, 1);
		vp = s;	/* prepare for next round */
	    }
	    buf[0] = '\0';	/* option already done */
	}

	if (tp - tags < sizeof(tags) - 5) {	/* add tag to the list */
	    if (tp != tags)
		*tp++ = FLD_SEP;
	    sprintf(tp, "%d", tag);
	    tp += strlen(tp);
	}
	if (buf[0]) {
	    char	env[128];	/* the string name */

	    if (op->tag == 0)
		sprintf(env, op->desc, opts[0].desc, tag);
	    else
		sprintf(env, "%s%s", opts[0].desc, op->desc);
	    /*
	     * Do not replace existing values in the environment, so that
	     * locally-obtained values can override server-provided values.
	     */
	    setenv(env, buf, 0);
	}
    }
    if (tp != tags) {
	char	env[128];	/* the string name */
	sprintf(env, "%stags", opts[0].desc);
	setenv(env, tags, 1);
    }
}
#endif /* additional dhcp */
