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
 *	$KAME: in6_ifattach.c,v 1.118 2001/05/24 07:44:00 itojun Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/proc.h>
#include <sys/rmlock.h>
#include <sys/syslog.h>
#include <sys/md5.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/route.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/if_ether.h>
#include <netinet/in_pcb.h>
#include <netinet/ip_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>

#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet6/in6_var.h>
#include <netinet6/in6_pcb.h>
#include <netinet6/in6_ifattach.h>
#include <netinet6/ip6_var.h>
#include <netinet6/nd6.h>
#include <netinet6/mld6_var.h>
#include <netinet6/scope6_var.h>

VNET_DEFINE(unsigned long, in6_maxmtu) = 0;

#ifdef IP6_AUTO_LINKLOCAL
VNET_DEFINE(int, ip6_auto_linklocal) = IP6_AUTO_LINKLOCAL;
#else
VNET_DEFINE(int, ip6_auto_linklocal) = 1;	/* enabled by default */
#endif

VNET_DEFINE(struct callout, in6_tmpaddrtimer_ch);
#define	V_in6_tmpaddrtimer_ch		VNET(in6_tmpaddrtimer_ch)

VNET_DECLARE(struct inpcbinfo, ripcbinfo);
#define	V_ripcbinfo			VNET(ripcbinfo)

static int get_rand_ifid(struct ifnet *, struct in6_addr *);
static int generate_tmp_ifid(u_int8_t *, const u_int8_t *, u_int8_t *);
static int get_ifid(struct ifnet *, struct ifnet *, struct in6_addr *);
static int in6_ifattach_linklocal(struct ifnet *, struct ifnet *);
static int in6_ifattach_loopback(struct ifnet *);
static void in6_purgemaddrs(struct ifnet *);

#define EUI64_GBIT	0x01
#define EUI64_UBIT	0x02
#define EUI64_TO_IFID(in6)	do {(in6)->s6_addr[8] ^= EUI64_UBIT; } while (0)
#define EUI64_GROUP(in6)	((in6)->s6_addr[8] & EUI64_GBIT)
#define EUI64_INDIVIDUAL(in6)	(!EUI64_GROUP(in6))
#define EUI64_LOCAL(in6)	((in6)->s6_addr[8] & EUI64_UBIT)
#define EUI64_UNIVERSAL(in6)	(!EUI64_LOCAL(in6))

#define IFID_LOCAL(in6)		(!EUI64_LOCAL(in6))
#define IFID_UNIVERSAL(in6)	(!EUI64_UNIVERSAL(in6))

/*
 * Generate a last-resort interface identifier, when the machine has no
 * IEEE802/EUI64 address sources.
 * The goal here is to get an interface identifier that is
 * (1) random enough and (2) does not change across reboot.
 * We currently use MD5(hostname) for it.
 *
 * in6 - upper 64bits are preserved
 */
static int
get_rand_ifid(struct ifnet *ifp, struct in6_addr *in6)
{
	MD5_CTX ctxt;
	struct prison *pr;
	u_int8_t digest[16];
	int hostnamelen;

	pr = curthread->td_ucred->cr_prison;
	mtx_lock(&pr->pr_mtx);
	hostnamelen = strlen(pr->pr_hostname);
#if 0
	/* we need at least several letters as seed for ifid */
	if (hostnamelen < 3) {
		mtx_unlock(&pr->pr_mtx);
		return -1;
	}
#endif

	/* generate 8 bytes of pseudo-random value. */
	bzero(&ctxt, sizeof(ctxt));
	MD5Init(&ctxt);
	MD5Update(&ctxt, pr->pr_hostname, hostnamelen);
	mtx_unlock(&pr->pr_mtx);
	MD5Final(digest, &ctxt);

	/* assumes sizeof(digest) > sizeof(ifid) */
	bcopy(digest, &in6->s6_addr[8], 8);

	/* make sure to set "u" bit to local, and "g" bit to individual. */
	in6->s6_addr[8] &= ~EUI64_GBIT;	/* g bit to "individual" */
	in6->s6_addr[8] |= EUI64_UBIT;	/* u bit to "local" */

	/* convert EUI64 into IPv6 interface identifier */
	EUI64_TO_IFID(in6);

	return 0;
}

static int
generate_tmp_ifid(u_int8_t *seed0, const u_int8_t *seed1, u_int8_t *ret)
{
	MD5_CTX ctxt;
	u_int8_t seed[16], digest[16], nullbuf[8];
	u_int32_t val32;

	/* If there's no history, start with a random seed. */
	bzero(nullbuf, sizeof(nullbuf));
	if (bcmp(nullbuf, seed0, sizeof(nullbuf)) == 0) {
		int i;

		for (i = 0; i < 2; i++) {
			val32 = arc4random();
			bcopy(&val32, seed + sizeof(val32) * i, sizeof(val32));
		}
	} else
		bcopy(seed0, seed, 8);

	/* copy the right-most 64-bits of the given address */
	/* XXX assumption on the size of IFID */
	bcopy(seed1, &seed[8], 8);

	if (0) {		/* for debugging purposes only */
		int i;

		printf("generate_tmp_ifid: new randomized ID from: ");
		for (i = 0; i < 16; i++)
			printf("%02x", seed[i]);
		printf(" ");
	}

	/* generate 16 bytes of pseudo-random value. */
	bzero(&ctxt, sizeof(ctxt));
	MD5Init(&ctxt);
	MD5Update(&ctxt, seed, sizeof(seed));
	MD5Final(digest, &ctxt);

	/*
	 * RFC 3041 3.2.1. (3)
	 * Take the left-most 64-bits of the MD5 digest and set bit 6 (the
	 * left-most bit is numbered 0) to zero.
	 */
	bcopy(digest, ret, 8);
	ret[0] &= ~EUI64_UBIT;

	/*
	 * XXX: we'd like to ensure that the generated value is not zero
	 * for simplicity.  If the caclculated digest happens to be zero,
	 * use a random non-zero value as the last resort.
	 */
	if (bcmp(nullbuf, ret, sizeof(nullbuf)) == 0) {
		nd6log((LOG_INFO,
		    "generate_tmp_ifid: computed MD5 value is zero.\n"));

		val32 = arc4random();
		val32 = 1 + (val32 % (0xffffffff - 1));
	}

	/*
	 * RFC 3041 3.2.1. (4)
	 * Take the rightmost 64-bits of the MD5 digest and save them in
	 * stable storage as the history value to be used in the next
	 * iteration of the algorithm.
	 */
	bcopy(&digest[8], seed0, 8);

	if (0) {		/* for debugging purposes only */
		int i;

		printf("to: ");
		for (i = 0; i < 16; i++)
			printf("%02x", digest[i]);
		printf("\n");
	}

	return 0;
}

/*
 * Get interface identifier for the specified interface.
 * XXX assumes single sockaddr_dl (AF_LINK address) per an interface
 *
 * in6 - upper 64bits are preserved
 */
int
in6_get_hw_ifid(struct ifnet *ifp, struct in6_addr *in6)
{
	struct epoch_tracker et;
	struct ifaddr *ifa;
	struct sockaddr_dl *sdl;
	u_int8_t *addr;
	size_t addrlen;
	static u_int8_t allzero[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	static u_int8_t allone[8] =
		{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

	NET_EPOCH_ENTER(et);
	CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
		if (ifa->ifa_addr->sa_family != AF_LINK)
			continue;
		sdl = (struct sockaddr_dl *)ifa->ifa_addr;
		if (sdl == NULL)
			continue;
		if (sdl->sdl_alen == 0)
			continue;

		goto found;
	}
	NET_EPOCH_EXIT(et);

	return -1;

found:
	IF_ADDR_LOCK_ASSERT(ifp);
	addr = LLADDR(sdl);
	addrlen = sdl->sdl_alen;

	/* get EUI64 */
	switch (ifp->if_type) {
	case IFT_BRIDGE:
	case IFT_ETHER:
	case IFT_L2VLAN:
	case IFT_ATM:
	case IFT_IEEE1394:
		/* IEEE802/EUI64 cases - what others? */
		/* IEEE1394 uses 16byte length address starting with EUI64 */
		if (addrlen > 8)
			addrlen = 8;

		/* look at IEEE802/EUI64 only */
		if (addrlen != 8 && addrlen != 6) {
			NET_EPOCH_EXIT(et);
			return -1;
		}

		/*
		 * check for invalid MAC address - on bsdi, we see it a lot
		 * since wildboar configures all-zero MAC on pccard before
		 * card insertion.
		 */
		if (bcmp(addr, allzero, addrlen) == 0) {
			NET_EPOCH_EXIT(et);
			return -1;
		}
		if (bcmp(addr, allone, addrlen) == 0) {
			NET_EPOCH_EXIT(et);
			return -1;
		}

		/* make EUI64 address */
		if (addrlen == 8)
			bcopy(addr, &in6->s6_addr[8], 8);
		else if (addrlen == 6) {
			in6->s6_addr[8] = addr[0];
			in6->s6_addr[9] = addr[1];
			in6->s6_addr[10] = addr[2];
			in6->s6_addr[11] = 0xff;
			in6->s6_addr[12] = 0xfe;
			in6->s6_addr[13] = addr[3];
			in6->s6_addr[14] = addr[4];
			in6->s6_addr[15] = addr[5];
		}
		break;

	case IFT_GIF:
	case IFT_STF:
		/*
		 * RFC2893 says: "SHOULD use IPv4 address as ifid source".
		 * however, IPv4 address is not very suitable as unique
		 * identifier source (can be renumbered).
		 * we don't do this.
		 */
		NET_EPOCH_EXIT(et);
		return -1;

	default:
		NET_EPOCH_EXIT(et);
		return -1;
	}

	/* sanity check: g bit must not indicate "group" */
	if (EUI64_GROUP(in6)) {
		NET_EPOCH_EXIT(et);
		return -1;
	}

	/* convert EUI64 into IPv6 interface identifier */
	EUI64_TO_IFID(in6);

	/*
	 * sanity check: ifid must not be all zero, avoid conflict with
	 * subnet router anycast
	 */
	if ((in6->s6_addr[8] & ~(EUI64_GBIT | EUI64_UBIT)) == 0x00 &&
	    bcmp(&in6->s6_addr[9], allzero, 7) == 0) {
		NET_EPOCH_EXIT(et);
		return -1;
	}

	NET_EPOCH_EXIT(et);
	return 0;
}

/*
 * Get interface identifier for the specified interface.  If it is not
 * available on ifp0, borrow interface identifier from other information
 * sources.
 *
 * altifp - secondary EUI64 source
 */
static int
get_ifid(struct ifnet *ifp0, struct ifnet *altifp,
    struct in6_addr *in6)
{
	struct epoch_tracker et;
	struct ifnet *ifp;

	/* first, try to get it from the interface itself */
	if (in6_get_hw_ifid(ifp0, in6) == 0) {
		nd6log((LOG_DEBUG, "%s: got interface identifier from itself\n",
		    if_name(ifp0)));
		goto success;
	}

	/* try secondary EUI64 source. this basically is for ATM PVC */
	if (altifp && in6_get_hw_ifid(altifp, in6) == 0) {
		nd6log((LOG_DEBUG, "%s: got interface identifier from %s\n",
		    if_name(ifp0), if_name(altifp)));
		goto success;
	}

	/* next, try to get it from some other hardware interface */
	NET_EPOCH_ENTER(et);
	CK_STAILQ_FOREACH(ifp, &V_ifnet, if_link) {
		if (ifp == ifp0)
			continue;
		if (in6_get_hw_ifid(ifp, in6) != 0)
			continue;

		/*
		 * to borrow ifid from other interface, ifid needs to be
		 * globally unique
		 */
		if (IFID_UNIVERSAL(in6)) {
			nd6log((LOG_DEBUG,
			    "%s: borrow interface identifier from %s\n",
			    if_name(ifp0), if_name(ifp)));
			NET_EPOCH_EXIT(et);
			goto success;
		}
	}
	NET_EPOCH_EXIT(et);

	/* last resort: get from random number source */
	if (get_rand_ifid(ifp, in6) == 0) {
		nd6log((LOG_DEBUG,
		    "%s: interface identifier generated by random number\n",
		    if_name(ifp0)));
		goto success;
	}

	printf("%s: failed to get interface identifier\n", if_name(ifp0));
	return -1;

success:
	nd6log((LOG_INFO, "%s: ifid: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
	    if_name(ifp0), in6->s6_addr[8], in6->s6_addr[9], in6->s6_addr[10],
	    in6->s6_addr[11], in6->s6_addr[12], in6->s6_addr[13],
	    in6->s6_addr[14], in6->s6_addr[15]));
	return 0;
}

/*
 * altifp - secondary EUI64 source
 */
static int
in6_ifattach_linklocal(struct ifnet *ifp, struct ifnet *altifp)
{
	struct in6_ifaddr *ia;
	struct in6_aliasreq ifra;
	struct nd_prefixctl pr0;
	struct nd_prefix *pr;
	int error;

	/*
	 * configure link-local address.
	 */
	in6_prepare_ifra(&ifra, NULL, &in6mask64);

	ifra.ifra_addr.sin6_addr.s6_addr32[0] = htonl(0xfe800000);
	ifra.ifra_addr.sin6_addr.s6_addr32[1] = 0;
	if ((ifp->if_flags & IFF_LOOPBACK) != 0) {
		ifra.ifra_addr.sin6_addr.s6_addr32[2] = 0;
		ifra.ifra_addr.sin6_addr.s6_addr32[3] = htonl(1);
	} else {
		if (get_ifid(ifp, altifp, &ifra.ifra_addr.sin6_addr) != 0) {
			nd6log((LOG_ERR,
			    "%s: no ifid available\n", if_name(ifp)));
			return (-1);
		}
	}
	if (in6_setscope(&ifra.ifra_addr.sin6_addr, ifp, NULL))
		return (-1);

	/* link-local addresses should NEVER expire. */
	ifra.ifra_lifetime.ia6t_vltime = ND6_INFINITE_LIFETIME;
	ifra.ifra_lifetime.ia6t_pltime = ND6_INFINITE_LIFETIME;

	/*
	 * Now call in6_update_ifa() to do a bunch of procedures to configure
	 * a link-local address. We can set the 3rd argument to NULL, because
	 * we know there's no other link-local address on the interface
	 * and therefore we are adding one (instead of updating one).
	 */
	if ((error = in6_update_ifa(ifp, &ifra, NULL,
				    IN6_IFAUPDATE_DADDELAY)) != 0) {
		/*
		 * XXX: When the interface does not support IPv6, this call
		 * would fail in the SIOCSIFADDR ioctl.  I believe the
		 * notification is rather confusing in this case, so just
		 * suppress it.  (jinmei@kame.net 20010130)
		 */
		if (error != EAFNOSUPPORT)
			nd6log((LOG_NOTICE, "in6_ifattach_linklocal: failed to "
			    "configure a link-local address on %s "
			    "(errno=%d)\n",
			    if_name(ifp), error));
		return (-1);
	}

	ia = in6ifa_ifpforlinklocal(ifp, 0);
	if (ia == NULL) {
		/*
		 * Another thread removed the address that we just added.
		 * This should be rare, but it happens.
		 */
		nd6log((LOG_NOTICE, "%s: %s: new link-local address "
			"disappeared\n", __func__, if_name(ifp)));
		return (-1);
	}
	ifa_free(&ia->ia_ifa);

	/*
	 * Make the link-local prefix (fe80::%link/64) as on-link.
	 * Since we'd like to manage prefixes separately from addresses,
	 * we make an ND6 prefix structure for the link-local prefix,
	 * and add it to the prefix list as a never-expire prefix.
	 * XXX: this change might affect some existing code base...
	 */
	bzero(&pr0, sizeof(pr0));
	pr0.ndpr_ifp = ifp;
	/* this should be 64 at this moment. */
	pr0.ndpr_plen = in6_mask2len(&ifra.ifra_prefixmask.sin6_addr, NULL);
	pr0.ndpr_prefix = ifra.ifra_addr;
	/* apply the mask for safety. (nd6_prelist_add will apply it again) */
	IN6_MASK_ADDR(&pr0.ndpr_prefix.sin6_addr, &in6mask64);
	/*
	 * Initialize parameters.  The link-local prefix must always be
	 * on-link, and its lifetimes never expire.
	 */
	pr0.ndpr_raf_onlink = 1;
	pr0.ndpr_raf_auto = 1;	/* probably meaningless */
	pr0.ndpr_vltime = ND6_INFINITE_LIFETIME;
	pr0.ndpr_pltime = ND6_INFINITE_LIFETIME;
	/*
	 * Since there is no other link-local addresses, nd6_prefix_lookup()
	 * probably returns NULL.  However, we cannot always expect the result.
	 * For example, if we first remove the (only) existing link-local
	 * address, and then reconfigure another one, the prefix is still
	 * valid with referring to the old link-local address.
	 */
	if ((pr = nd6_prefix_lookup(&pr0)) == NULL) {
		if ((error = nd6_prelist_add(&pr0, NULL, NULL)) != 0)
			return (error);
	} else
		nd6_prefix_rele(pr);

	return 0;
}

/*
 * ifp - must be IFT_LOOP
 */
static int
in6_ifattach_loopback(struct ifnet *ifp)
{
	struct in6_aliasreq ifra;
	int error;

	in6_prepare_ifra(&ifra, &in6addr_loopback, &in6mask128);

	/*
	 * Always initialize ia_dstaddr (= broadcast address) to loopback
	 * address.  Follows IPv4 practice - see in_ifinit().
	 */
	ifra.ifra_dstaddr.sin6_len = sizeof(struct sockaddr_in6);
	ifra.ifra_dstaddr.sin6_family = AF_INET6;
	ifra.ifra_dstaddr.sin6_addr = in6addr_loopback;

	/* the loopback  address should NEVER expire. */
	ifra.ifra_lifetime.ia6t_vltime = ND6_INFINITE_LIFETIME;
	ifra.ifra_lifetime.ia6t_pltime = ND6_INFINITE_LIFETIME;

	/*
	 * We are sure that this is a newly assigned address, so we can set
	 * NULL to the 3rd arg.
	 */
	if ((error = in6_update_ifa(ifp, &ifra, NULL, 0)) != 0) {
		nd6log((LOG_ERR, "in6_ifattach_loopback: failed to configure "
		    "the loopback address on %s (errno=%d)\n",
		    if_name(ifp), error));
		return (-1);
	}

	return 0;
}

/*
 * compute NI group address, based on the current hostname setting.
 * see RFC 4620.
 *
 * when ifp == NULL, the caller is responsible for filling scopeid.
 *
 * If oldmcprefix == 1, FF02:0:0:0:0:2::/96 is used for NI group address
 * while it is FF02:0:0:0:0:2:FF00::/104 in RFC 4620. 
 */
static int
in6_nigroup0(struct ifnet *ifp, const char *name, int namelen,
    struct in6_addr *in6, int oldmcprefix)
{
	struct prison *pr;
	const char *p;
	u_char *q;
	MD5_CTX ctxt;
	u_int8_t digest[16];
	char l;
	char n[64];	/* a single label must not exceed 63 chars */

	/*
	 * If no name is given and namelen is -1,
	 * we try to do the hostname lookup ourselves.
	 */
	if (!name && namelen == -1) {
		pr = curthread->td_ucred->cr_prison;
		mtx_lock(&pr->pr_mtx);
		name = pr->pr_hostname;
		namelen = strlen(name);
	} else
		pr = NULL;
	if (!name || !namelen) {
		if (pr != NULL)
			mtx_unlock(&pr->pr_mtx);
		return -1;
	}

	p = name;
	while (p && *p && *p != '.' && p - name < namelen)
		p++;
	if (p == name || p - name > sizeof(n) - 1) {
		if (pr != NULL)
			mtx_unlock(&pr->pr_mtx);
		return -1;	/* label too long */
	}
	l = p - name;
	strncpy(n, name, l);
	if (pr != NULL)
		mtx_unlock(&pr->pr_mtx);
	n[(int)l] = '\0';
	for (q = n; *q; q++) {
		if ('A' <= *q && *q <= 'Z')
			*q = *q - 'A' + 'a';
	}

	/* generate 16 bytes of pseudo-random value. */
	bzero(&ctxt, sizeof(ctxt));
	MD5Init(&ctxt);
	MD5Update(&ctxt, &l, sizeof(l));
	MD5Update(&ctxt, n, l);
	MD5Final(digest, &ctxt);

	bzero(in6, sizeof(*in6));
	in6->s6_addr16[0] = IPV6_ADDR_INT16_MLL;
	in6->s6_addr8[11] = 2;
	if (oldmcprefix == 0) {
		in6->s6_addr8[12] = 0xff;
	 	/* Copy the first 24 bits of 128-bit hash into the address. */
		bcopy(digest, &in6->s6_addr8[13], 3);
	} else {
	 	/* Copy the first 32 bits of 128-bit hash into the address. */
		bcopy(digest, &in6->s6_addr32[3], sizeof(in6->s6_addr32[3]));
	}
	if (in6_setscope(in6, ifp, NULL))
		return (-1); /* XXX: should not fail */

	return 0;
}

int
in6_nigroup(struct ifnet *ifp, const char *name, int namelen,
    struct in6_addr *in6)
{

	return (in6_nigroup0(ifp, name, namelen, in6, 0));
}

int
in6_nigroup_oldmcprefix(struct ifnet *ifp, const char *name, int namelen,
    struct in6_addr *in6)
{

	return (in6_nigroup0(ifp, name, namelen, in6, 1));
}

/*
 * XXX multiple loopback interface needs more care.  for instance,
 * nodelocal address needs to be configured onto only one of them.
 * XXX multiple link-local address case
 *
 * altifp - secondary EUI64 source
 */
void
in6_ifattach(struct ifnet *ifp, struct ifnet *altifp)
{
	struct in6_ifaddr *ia;

	if (ifp->if_afdata[AF_INET6] == NULL)
		return;
	/*
	 * quirks based on interface type
	 */
	switch (ifp->if_type) {
	case IFT_STF:
		/*
		 * 6to4 interface is a very special kind of beast.
		 * no multicast, no linklocal.  RFC2529 specifies how to make
		 * linklocals for 6to4 interface, but there's no use and
		 * it is rather harmful to have one.
		 */
		ND_IFINFO(ifp)->flags &= ~ND6_IFF_AUTO_LINKLOCAL;
		break;
	default:
		break;
	}

	/*
	 * usually, we require multicast capability to the interface
	 */
	if ((ifp->if_flags & IFF_MULTICAST) == 0) {
		nd6log((LOG_INFO, "in6_ifattach: "
		    "%s is not multicast capable, IPv6 not enabled\n",
		    if_name(ifp)));
		return;
	}

	/*
	 * assign loopback address for loopback interface.
	 */
	if ((ifp->if_flags & IFF_LOOPBACK) != 0) {
		/*
		 * check that loopback address doesn't exist yet.
		 */
		ia = in6ifa_ifwithaddr(&in6addr_loopback, 0);
		if (ia == NULL)
			in6_ifattach_loopback(ifp);
		else
			ifa_free(&ia->ia_ifa);
	}

	/*
	 * assign a link-local address, if there's none.
	 */
	if (!(ND_IFINFO(ifp)->flags & ND6_IFF_IFDISABLED) &&
	    ND_IFINFO(ifp)->flags & ND6_IFF_AUTO_LINKLOCAL) {
		ia = in6ifa_ifpforlinklocal(ifp, 0);
		if (ia == NULL)
			in6_ifattach_linklocal(ifp, altifp);
		else
			ifa_free(&ia->ia_ifa);
	}

	/* update dynamically. */
	if (V_in6_maxmtu < ifp->if_mtu)
		V_in6_maxmtu = ifp->if_mtu;
}

/*
 * NOTE: in6_ifdetach() does not support loopback if at this moment.
 *
 * When shutting down a VNET we clean up layers top-down.  In that case
 * upper layer protocols (ulp) are cleaned up already and locks are destroyed
 * and we must not call into these cleanup functions anymore, thus purgeulp
 * is set to 0 in that case by in6_ifdetach_destroy().
 * The normal case of destroying a (cloned) interface still needs to cleanup
 * everything related to the interface and will have purgeulp set to 1.
 */
static void
_in6_ifdetach(struct ifnet *ifp, int purgeulp)
{
	struct ifaddr *ifa, *next;

	if (ifp->if_afdata[AF_INET6] == NULL)
		return;

	/*
	 * nuke any of IPv6 addresses we have
	 */
	CK_STAILQ_FOREACH_SAFE(ifa, &ifp->if_addrhead, ifa_link, next) {
		if (ifa->ifa_addr->sa_family != AF_INET6)
			continue;
		in6_purgeaddr(ifa);
	}
	if (purgeulp) {
		in6_pcbpurgeif0(&V_udbinfo, ifp);
		in6_pcbpurgeif0(&V_ulitecbinfo, ifp);
		in6_pcbpurgeif0(&V_ripcbinfo, ifp);
	}
	/* leave from all multicast groups joined */
	in6_purgemaddrs(ifp);

	/*
	 * Remove neighbor management table.
	 * Enabling the nd6_purge will panic on vmove for interfaces on VNET
	 * teardown as the IPv6 layer is cleaned up already and the locks
	 * are destroyed.
	 */
	if (purgeulp)
		nd6_purge(ifp);
}

void
in6_ifdetach(struct ifnet *ifp)
{

	_in6_ifdetach(ifp, 1);
}

void
in6_ifdetach_destroy(struct ifnet *ifp)
{

	_in6_ifdetach(ifp, 0);
}

int
in6_get_tmpifid(struct ifnet *ifp, u_int8_t *retbuf,
    const u_int8_t *baseid, int generate)
{
	u_int8_t nullbuf[8];
	struct nd_ifinfo *ndi = ND_IFINFO(ifp);

	bzero(nullbuf, sizeof(nullbuf));
	if (bcmp(ndi->randomid, nullbuf, sizeof(nullbuf)) == 0) {
		/* we've never created a random ID.  Create a new one. */
		generate = 1;
	}

	if (generate) {
		bcopy(baseid, ndi->randomseed1, sizeof(ndi->randomseed1));

		/* generate_tmp_ifid will update seedn and buf */
		(void)generate_tmp_ifid(ndi->randomseed0, ndi->randomseed1,
		    ndi->randomid);
	}
	bcopy(ndi->randomid, retbuf, 8);

	return (0);
}

void
in6_tmpaddrtimer(void *arg)
{
	CURVNET_SET((struct vnet *) arg);
	struct nd_ifinfo *ndi;
	u_int8_t nullbuf[8];
	struct ifnet *ifp;

	callout_reset(&V_in6_tmpaddrtimer_ch,
	    (V_ip6_temp_preferred_lifetime - V_ip6_desync_factor -
	    V_ip6_temp_regen_advance) * hz, in6_tmpaddrtimer, curvnet);

	bzero(nullbuf, sizeof(nullbuf));
	CK_STAILQ_FOREACH(ifp, &V_ifnet, if_link) {
		if (ifp->if_afdata[AF_INET6] == NULL)
			continue;
		ndi = ND_IFINFO(ifp);
		if (bcmp(ndi->randomid, nullbuf, sizeof(nullbuf)) != 0) {
			/*
			 * We've been generating a random ID on this interface.
			 * Create a new one.
			 */
			(void)generate_tmp_ifid(ndi->randomseed0,
			    ndi->randomseed1, ndi->randomid);
		}
	}

	CURVNET_RESTORE();
}

static void
in6_purgemaddrs(struct ifnet *ifp)
{
	struct in6_multi_head inmh;

	SLIST_INIT(&inmh);
	IN6_MULTI_LOCK();
	IN6_MULTI_LIST_LOCK();
	mld_ifdetach(ifp, &inmh);
	IN6_MULTI_LIST_UNLOCK();
	IN6_MULTI_UNLOCK();
	in6m_release_list_deferred(&inmh);

	/*
	 * Make sure all multicast deletions invoking if_ioctl() are
	 * completed before returning. Else we risk accessing a freed
	 * ifnet structure pointer.
	 */
	in6m_release_wait();
}

void
in6_ifattach_destroy(void)
{

	callout_drain(&V_in6_tmpaddrtimer_ch);
}

static void
in6_ifattach_init(void *dummy)
{

	/* Timer for regeneranation of temporary addresses randomize ID. */
	callout_init(&V_in6_tmpaddrtimer_ch, 0);
	callout_reset(&V_in6_tmpaddrtimer_ch,
	    (V_ip6_temp_preferred_lifetime - V_ip6_desync_factor -
	    V_ip6_temp_regen_advance) * hz,
	    in6_tmpaddrtimer, curvnet);
}

/*
 * Cheat.
 * This must be after route_init(), which is now SI_ORDER_THIRD.
 */
SYSINIT(in6_ifattach_init, SI_SUB_PROTO_DOMAIN, SI_ORDER_MIDDLE,
    in6_ifattach_init, NULL);
