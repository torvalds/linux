/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
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
 *	from: @(#)autoconf.c	7.1 (Berkeley) 5/9/91
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_bootp.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/if_var.h>
#include <net/ethernet.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <nfs/nfsproto.h>
#include <nfsclient/nfs.h>
#include <nfs/nfsdiskless.h>

#define	NFS_IFACE_TIMEOUT_SECS	10 /* Timeout for interface to appear. */

static int inaddr_to_sockaddr(char *ev, struct sockaddr_in *sa);
static int hwaddr_to_sockaddr(char *ev, struct sockaddr_dl *sa);
static int decode_nfshandle(char *ev, u_char *fh, int maxfh);

/*
 * This structure must be filled in by a primary bootstrap or bootstrap
 * server for a diskless/dataless machine. It is initialized below just
 * to ensure that it is allocated to initialized data (.data not .bss).
 */
struct nfs_diskless	nfs_diskless = { { { 0 } } };
struct nfsv3_diskless	nfsv3_diskless = { { { 0 } } };
int			nfs_diskless_valid = 0;

/*
 * Validate/sanity check a rsize/wsize parameter.
 */
static int
checkrwsize(unsigned long v, const char *name)
{
	/*
	 * 32K is used as an upper bound because most servers
	 * limit block size to satisfy IPv4's limit of
	 * 64K/reassembled packet.  The lower bound is pretty
	 * much arbitrary.
	 */
	if (!(4 <= v && v <= 32*1024)) {
		printf("nfs_parse_options: invalid %s %lu ignored\n", name, v);
		return 0;
	} else
		return 1;
}

/*
 * Parse mount options and apply them to the supplied
 * nfs_diskless state.  Used also by bootp/dhcp support.
 */
void
nfs_parse_options(const char *envopts, struct nfs_args *nd)
{
	char *opts, *o, *otmp;
	unsigned long v;

	opts = strdup(envopts, M_TEMP);
	otmp = opts;
	while ((o = strsep(&otmp, ":;, ")) != NULL) {
		if (*o == '\0')
			; /* Skip empty options. */
		else if (strcmp(o, "soft") == 0)
			nd->flags |= NFSMNT_SOFT;
		else if (strcmp(o, "intr") == 0)
			nd->flags |= NFSMNT_INT;
		else if (strcmp(o, "conn") == 0)
			nd->flags |= NFSMNT_NOCONN;
		else if (strcmp(o, "nolockd") == 0)
			nd->flags |= NFSMNT_NOLOCKD;
		else if (strcmp(o, "nocto") == 0)
			nd->flags |= NFSMNT_NOCTO;
		else if (strcmp(o, "nfsv2") == 0)
			nd->flags &= ~(NFSMNT_NFSV3 | NFSMNT_NFSV4);
		else if (strcmp(o, "nfsv3") == 0) {
			nd->flags &= ~NFSMNT_NFSV4;
			nd->flags |= NFSMNT_NFSV3;
		} else if (strcmp(o, "tcp") == 0)
			nd->sotype = SOCK_STREAM;
		else if (strcmp(o, "udp") == 0)
			nd->sotype = SOCK_DGRAM;
		else if (strncmp(o, "rsize=", 6) == 0) {
			v = strtoul(o+6, NULL, 10);
			if (checkrwsize(v, "rsize")) {
				nd->rsize = (int) v;
				nd->flags |= NFSMNT_RSIZE;
			}
		} else if (strncmp(o, "wsize=", 6) == 0) {
			v = strtoul(o+6, NULL, 10);
			if (checkrwsize(v, "wsize")) {
				nd->wsize = (int) v;
				nd->flags |= NFSMNT_WSIZE;
			}
		} else
			printf("%s: skipping unknown option \"%s\"\n",
			    __func__, o);
	}
	free(opts, M_TEMP);
}

/*
 * Populate the essential fields in the nfsv3_diskless structure.
 *
 * The loader is expected to export the following environment variables:
 *
 * boot.netif.name		name of boot interface
 * boot.netif.ip		IP address on boot interface
 * boot.netif.netmask		netmask on boot interface
 * boot.netif.gateway		default gateway (optional)
 * boot.netif.hwaddr		hardware address of boot interface
 * boot.netif.mtu		interface mtu from bootp/dhcp (optional)
 * boot.nfsroot.server		IP address of root filesystem server
 * boot.nfsroot.path		path of the root filesystem on server
 * boot.nfsroot.nfshandle	NFS handle for root filesystem on server
 * boot.nfsroot.nfshandlelen	and length of this handle (for NFSv3 only)
 * boot.nfsroot.options		NFS options for the root filesystem
 */
void
nfs_setup_diskless(void)
{
	struct nfs_diskless *nd = &nfs_diskless;
	struct nfsv3_diskless *nd3 = &nfsv3_diskless;
	struct ifnet *ifp;
	struct ifaddr *ifa;
	struct sockaddr_dl *sdl, ourdl;
	struct sockaddr_in myaddr, netmask;
	char *cp;
	int cnt, fhlen, is_nfsv3;
	uint32_t len;
	time_t timeout_at;

	if (nfs_diskless_valid != 0)
		return;

	/* get handle size. If this succeeds, it's an NFSv3 setup. */
	if ((cp = kern_getenv("boot.nfsroot.nfshandlelen")) != NULL) {
		cnt = sscanf(cp, "%d", &len);
		freeenv(cp);
		if (cnt != 1 || len == 0 || len > NFSX_V3FHMAX) {
			printf("nfs_diskless: bad NFS handle len\n");
			return;
		}
		nd3->root_fhsize = len;
		is_nfsv3 = 1;
	} else
		is_nfsv3 = 0;
	/* set up interface */
	if (inaddr_to_sockaddr("boot.netif.ip", &myaddr))
		return;
	if (inaddr_to_sockaddr("boot.netif.netmask", &netmask)) {
		printf("nfs_diskless: no netmask\n");
		return;
	}
	if (is_nfsv3 != 0) {
		bcopy(&myaddr, &nd3->myif.ifra_addr, sizeof(myaddr));
		bcopy(&myaddr, &nd3->myif.ifra_broadaddr, sizeof(myaddr));
		((struct sockaddr_in *) 
		   &nd3->myif.ifra_broadaddr)->sin_addr.s_addr =
		    myaddr.sin_addr.s_addr | ~ netmask.sin_addr.s_addr;
		bcopy(&netmask, &nd3->myif.ifra_mask, sizeof(netmask));
	} else {
		bcopy(&myaddr, &nd->myif.ifra_addr, sizeof(myaddr));
		bcopy(&myaddr, &nd->myif.ifra_broadaddr, sizeof(myaddr));
		((struct sockaddr_in *) 
		   &nd->myif.ifra_broadaddr)->sin_addr.s_addr =
		    myaddr.sin_addr.s_addr | ~ netmask.sin_addr.s_addr;
		bcopy(&netmask, &nd->myif.ifra_mask, sizeof(netmask));
	}

	if (hwaddr_to_sockaddr("boot.netif.hwaddr", &ourdl)) {
		printf("nfs_diskless: no hardware address\n");
		return;
	}
	ifa = NULL;
	timeout_at = time_uptime + NFS_IFACE_TIMEOUT_SECS;
retry:
	CURVNET_SET(TD_TO_VNET(curthread));
	IFNET_RLOCK();
	CK_STAILQ_FOREACH(ifp, &V_ifnet, if_link) {
		CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
			if (ifa->ifa_addr->sa_family == AF_LINK) {
				sdl = (struct sockaddr_dl *)ifa->ifa_addr;
				if ((sdl->sdl_type == ourdl.sdl_type) &&
				    (sdl->sdl_alen == ourdl.sdl_alen) &&
				    !bcmp(LLADDR(sdl),
					  LLADDR(&ourdl),
					  sdl->sdl_alen)) {
				    IFNET_RUNLOCK();
				    CURVNET_RESTORE();
				    goto match_done;
				}
			}
		}
	}
	IFNET_RUNLOCK();
	CURVNET_RESTORE();
	if (time_uptime < timeout_at) {
		pause("nfssdl", hz / 5);
		goto retry;
	}
	printf("nfs_diskless: no interface\n");
	return;	/* no matching interface */
match_done:
	kern_setenv("boot.netif.name", ifp->if_xname);
	if (is_nfsv3 != 0) {
		strlcpy(nd3->myif.ifra_name, ifp->if_xname,
		    sizeof(nd3->myif.ifra_name));
	
		/* set up gateway */
		inaddr_to_sockaddr("boot.netif.gateway", &nd3->mygateway);

		/* set up root mount */
		nd3->root_args.rsize = 32768;		/* XXX tunable? */
		nd3->root_args.wsize = 32768;
		nd3->root_args.sotype = SOCK_STREAM;
		nd3->root_args.flags = (NFSMNT_NFSV3 | NFSMNT_WSIZE |
		    NFSMNT_RSIZE | NFSMNT_RESVPORT);
		if (inaddr_to_sockaddr("boot.nfsroot.server",
		    &nd3->root_saddr)) {
			printf("nfs_diskless: no server\n");
			return;
		}
		nd3->root_saddr.sin_port = htons(NFS_PORT);
		fhlen = decode_nfshandle("boot.nfsroot.nfshandle",
		    &nd3->root_fh[0], NFSX_V3FHMAX);
		if (fhlen == 0) {
			printf("nfs_diskless: no NFS handle\n");
			return;
		}
		if (fhlen != nd3->root_fhsize) {
			printf("nfs_diskless: bad NFS handle len=%d\n", fhlen);
			return;
		}
		if ((cp = kern_getenv("boot.nfsroot.path")) != NULL) {
			strncpy(nd3->root_hostnam, cp, MNAMELEN - 1);
			freeenv(cp);
		}
		if ((cp = kern_getenv("boot.nfsroot.options")) != NULL) {
			nfs_parse_options(cp, &nd3->root_args);
			freeenv(cp);
		}
	
		nfs_diskless_valid = 3;
	} else {
		strlcpy(nd->myif.ifra_name, ifp->if_xname,
		    sizeof(nd->myif.ifra_name));
	
		/* set up gateway */
		inaddr_to_sockaddr("boot.netif.gateway", &nd->mygateway);

		/* set up root mount */
		nd->root_args.rsize = 8192;		/* XXX tunable? */
		nd->root_args.wsize = 8192;
		nd->root_args.sotype = SOCK_STREAM;
		nd->root_args.flags = (NFSMNT_WSIZE |
		    NFSMNT_RSIZE | NFSMNT_RESVPORT);
		if (inaddr_to_sockaddr("boot.nfsroot.server",
		    &nd->root_saddr)) {
			printf("nfs_diskless: no server\n");
			return;
		}
		nd->root_saddr.sin_port = htons(NFS_PORT);
		if (decode_nfshandle("boot.nfsroot.nfshandle",
		    &nd->root_fh[0], NFSX_V2FH) == 0) {
			printf("nfs_diskless: no NFS handle\n");
			return;
		}
		if ((cp = kern_getenv("boot.nfsroot.path")) != NULL) {
			strncpy(nd->root_hostnam, cp, MNAMELEN - 1);
			freeenv(cp);
		}
		if ((cp = kern_getenv("boot.nfsroot.options")) != NULL) {
			struct nfs_args args;
	
			/*
			 * XXX yech, convert between old and current
			 * arg format
			 */
			args.flags = nd->root_args.flags;
			args.sotype = nd->root_args.sotype;
			args.rsize = nd->root_args.rsize;
			args.wsize = nd->root_args.wsize;
			nfs_parse_options(cp, &args);
			nd->root_args.flags = args.flags;
			nd->root_args.sotype = args.sotype;
			nd->root_args.rsize = args.rsize;
			nd->root_args.wsize = args.wsize;
			freeenv(cp);
		}
	
		nfs_diskless_valid = 1;
	}
}

static int
inaddr_to_sockaddr(char *ev, struct sockaddr_in *sa)
{
	u_int32_t a[4];
	char *cp;
	int count;

	bzero(sa, sizeof(*sa));
	sa->sin_len = sizeof(*sa);
	sa->sin_family = AF_INET;

	if ((cp = kern_getenv(ev)) == NULL)
		return (1);
	count = sscanf(cp, "%d.%d.%d.%d", &a[0], &a[1], &a[2], &a[3]);
	freeenv(cp);
	if (count != 4)
		return (1);
	sa->sin_addr.s_addr =
	    htonl((a[0] << 24) | (a[1] << 16) | (a[2] << 8) | a[3]);
	return (0);
}

static int
hwaddr_to_sockaddr(char *ev, struct sockaddr_dl *sa)
{
	char *cp;
	u_int32_t a[6];
	int count;

	bzero(sa, sizeof(*sa));
	sa->sdl_len = sizeof(*sa);
	sa->sdl_family = AF_LINK;
	sa->sdl_type = IFT_ETHER;
	sa->sdl_alen = ETHER_ADDR_LEN;
	if ((cp = kern_getenv(ev)) == NULL)
		return (1);
	count = sscanf(cp, "%x:%x:%x:%x:%x:%x",
	    &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]);
	freeenv(cp);
	if (count != 6)
		return (1);
	sa->sdl_data[0] = a[0];
	sa->sdl_data[1] = a[1];
	sa->sdl_data[2] = a[2];
	sa->sdl_data[3] = a[3];
	sa->sdl_data[4] = a[4];
	sa->sdl_data[5] = a[5];
	return (0);
}

static int
decode_nfshandle(char *ev, u_char *fh, int maxfh) 
{
	u_char *cp, *ep;
	int len, val;

	ep = cp = kern_getenv(ev);
	if (cp == NULL)
		return (0);
	if ((strlen(cp) < 2) || (*cp != 'X')) {
		freeenv(ep);
		return (0);
	}
	len = 0;
	cp++;
	for (;;) {
		if (*cp == 'X') {
			freeenv(ep);
			return (len);
		}
		if ((sscanf(cp, "%2x", &val) != 1) || (val > 0xff)) {
			freeenv(ep);
			return (0);
		}
		*(fh++) = val;
		len++;
		cp += 2;
		if (len > maxfh) {
		    freeenv(ep);
		    return (0);
		}
	}
}

#if !defined(BOOTP_NFSROOT)
static void
nfs_rootconf(void)
{

	nfs_setup_diskless();
	if (nfs_diskless_valid)
		rootdevnames[0] = "nfs:";
}

SYSINIT(cpu_rootconf, SI_SUB_ROOT_CONF, SI_ORDER_FIRST, nfs_rootconf, NULL);
#endif

