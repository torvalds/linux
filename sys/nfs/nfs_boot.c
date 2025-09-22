/*	$OpenBSD: nfs_boot.c,v 1.49 2024/05/01 13:15:59 jsg Exp $ */
/*	$NetBSD: nfs_boot.c,v 1.26 1996/05/07 02:51:25 thorpej Exp $	*/

/*
 * Copyright (c) 1995 Adam Glass, Gordon Ross
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
 * 3. The name of the authors may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/queue.h>

#include <net/if.h>
#include <net/if_var.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/if_ether.h>

#include <nfs/rpcv2.h>
#include <nfs/nfsproto.h>
#include <nfs/nfs.h>
#include <nfs/nfsdiskless.h>
#include <nfs/krpc.h>
#include <nfs/xdr_subs.h>
#include <nfs/nfs_var.h>

#include "ether.h"

#if !defined(NFSCLIENT) || (NETHER == 0)

int
nfs_boot_init(struct nfs_diskless *nd, struct proc *procp)
{
	panic("nfs_boot_init: NFSCLIENT not enabled in kernel");
}

int
nfs_boot_getfh(struct sockaddr_in *bpsin, char *key,
    struct nfs_dlmount *ndmntp, int retries)
{
	/* can not get here */
	return (EOPNOTSUPP);
}

#else

/*
 * Support for NFS diskless booting, specifically getting information
 * about where to boot from, what pathnames, etc.
 *
 * This implementation uses RARP and the bootparam RPC.
 * We are forced to implement RPC anyway (to get file handles)
 * so we might as well take advantage of it for bootparam too.
 *
 * The diskless boot sequence goes as follows:
 * (1) Use RARP to get our interface address
 * (2) Use RPC/bootparam/whoami to get our hostname,
 *     our IP address, and the server's IP address.
 * (3) Use RPC/bootparam/getfile to get the root path
 * (4) Use RPC/mountd to get the root file handle
 * (5) Use RPC/bootparam/getfile to get the swap path
 * (6) Use RPC/mountd to get the swap file handle
 *
 * (This happens to be the way Sun does it too.)
 */

/* bootparam RPC */
static int bp_whoami(struct sockaddr_in *bpsin,
	struct in_addr *my_ip, struct in_addr *gw_ip);
static int bp_getfile(struct sockaddr_in *bpsin, char *key,
	struct sockaddr_in *mdsin, char *servname, char *path, int retries);

/* mountd RPC */
static int md_mount(struct sockaddr_in *mdsin, char *path,
	struct nfs_args *argp);

char	*nfsbootdevname;

/*
 * Called with an empty nfs_diskless struct to be filled in.
 */
int
nfs_boot_init(struct nfs_diskless *nd, struct proc *procp)
{
	struct ifreq ireq;
	struct in_aliasreq ifra;
	struct in_addr my_ip, gw_ip;
	struct sockaddr_in bp_sin;
	struct sockaddr_in *sin;
	struct ifnet *ifp;
	struct socket *so;
	struct ifaddr *ifa;
	char addr[INET_ADDRSTRLEN];
	int error;

	/*
	 * Find an interface, rarp for its ip address, stuff it, the
	 * implied broadcast addr, and netmask into a nfs_diskless struct.
	 *
	 * This was moved here from nfs_vfsops.c because this procedure
	 * would be quite different if someone decides to write (i.e.) a
	 * BOOTP version of this file (might not use RARP, etc.)
	 */

	/*
	 * Find a network interface.
	 */
	if (nfsbootdevname == NULL || (ifp = if_unit(nfsbootdevname)) == NULL)
		panic("nfs_boot: no suitable interface");

	bcopy(ifp->if_xname, ireq.ifr_name, IFNAMSIZ);
	printf("nfs_boot: using interface %s, with revarp & bootparams\n",
	    ireq.ifr_name);

	/*
	 * Bring up the interface.
	 *
	 * Get the old interface flags and or IFF_UP into them; if
	 * IFF_UP set blindly, interface selection can be clobbered.
	 */
	if ((error = socreate(AF_INET, &so, SOCK_DGRAM, 0)) != 0)
		panic("nfs_boot: socreate, error=%d", error);
	error = ifioctl(so, SIOCGIFFLAGS, (caddr_t)&ireq, procp);
	if (error)
		panic("nfs_boot: GIFFLAGS, error=%d", error);
	ireq.ifr_flags |= IFF_UP;
	error = ifioctl(so, SIOCSIFFLAGS, (caddr_t)&ireq, procp);
	if (error)
		panic("nfs_boot: SIFFLAGS, error=%d", error);

	/*
	 * Do RARP for the interface address.
	 */
	if ((error = revarpwhoami(&my_ip, ifp)) != 0)
		panic("reverse arp not answered by rarpd(8) or dhcpd(8)");
	inet_ntop(AF_INET, &my_ip, addr, sizeof(addr));
	printf("nfs_boot: client_addr=%s\n", addr);

	/*
	 * Do enough of ifconfig(8) so that the chosen interface
	 * can talk to the servers.  (just set the address)
	 */
	memset(&ifra, 0, sizeof(ifra));
	bcopy(ifp->if_xname, ifra.ifra_name, sizeof(ifra.ifra_name));

	sin = &ifra.ifra_addr;
	sin->sin_len = sizeof(*sin);
	sin->sin_family = AF_INET;
	sin->sin_addr.s_addr = my_ip.s_addr;
	error = ifioctl(so, SIOCAIFADDR, (caddr_t)&ifra, procp);
	if (error)
		panic("nfs_boot: set if addr, error=%d", error);

	soclose(so, 0);

	TAILQ_FOREACH(ifa, &ifp->if_addrlist, ifa_list) {
		if (ifa->ifa_addr->sa_family == AF_INET)
			break;
	}
	if (ifa == NULL)
		panic("nfs_boot: address not configured on %s", ifp->if_xname);
	if_put(ifp);

	/*
	 * Get client name and gateway address.
	 * RPC: bootparam/whoami
	 * The server address returned by the WHOAMI call
	 * is used for all subsequent bootparam RPCs.
	 */
	memset(&bp_sin, 0, sizeof(bp_sin));
	bp_sin.sin_len = sizeof(bp_sin);
	bp_sin.sin_family = AF_INET;
	bp_sin.sin_addr.s_addr = ifatoia(ifa)->ia_broadaddr.sin_addr.s_addr;
	hostnamelen = MAXHOSTNAMELEN;

	/* this returns gateway IP address */
	error = bp_whoami(&bp_sin, &my_ip, &gw_ip);
	if (error)
		panic("nfs_boot: bootparam whoami, error=%d", error);
	inet_ntop(AF_INET, &bp_sin.sin_addr, addr, sizeof(addr));
	printf("nfs_boot: server_addr=%s hostname=%s\n", addr, hostname);

	bcopy(&bp_sin, &nd->nd_boot, sizeof(bp_sin));

	return (0);
}

/*
 * bpsin:	bootparam server
 * key:		root or swap
 * ndmntp:	output
 */
int
nfs_boot_getfh(struct sockaddr_in *bpsin, char *key,
    struct nfs_dlmount *ndmntp, int retries)
{
	struct nfs_args *args;
	char pathname[MAXPATHLEN];
	char *sp, *dp, *endp;
	struct sockaddr_in *sin;
	int error;

	args = &ndmntp->ndm_args;

	/* Initialize mount args. */
	memset(args, 0, sizeof(*args));
	args->addr     = sintosa(&ndmntp->ndm_saddr);
	args->addrlen  = args->addr->sa_len;
	args->sotype   = SOCK_DGRAM;
	args->fh       = ndmntp->ndm_fh;
	args->hostname = ndmntp->ndm_host;
	args->flags    = NFSMNT_NFSV3;
#ifdef	NFS_BOOT_OPTIONS
	args->flags    |= NFS_BOOT_OPTIONS;
#endif
#ifdef	NFS_BOOT_RWSIZE
	/*
	 * Reduce rsize,wsize for interfaces that consistently
	 * drop fragments of long UDP messages.	 (i.e. wd8003).
	 * You can always change these later via remount.
	 */
	args->flags   |= NFSMNT_WSIZE | NFSMNT_RSIZE;
	args->wsize    = NFS_BOOT_RWSIZE;
	args->rsize    = NFS_BOOT_RWSIZE;
#endif

	sin = &ndmntp->ndm_saddr;

	/*
	 * Get server:pathname for "key" (root or swap)
	 * using RPC to bootparam/getfile
	 */
	error = bp_getfile(bpsin, key, sin, ndmntp->ndm_host, pathname,
	    retries);
	if (error) {
		printf("nfs_boot: bootparam get %s: %d\n", key, error);
		return (error);
	}

	/*
	 * Get file handle for "key" (root or swap)
	 * using RPC to mountd/mount
	 */
	error = md_mount(sin, pathname, args);
	if (error) {
		printf("nfs_boot: mountd %s, error=%d\n", key, error);
		return (error);
	}

	/* Set port number for NFS use. */
	/* XXX: NFS port is always 2049, right? */
	error = krpc_portmap(sin, NFS_PROG,
	    (args->flags & NFSMNT_NFSV3) ? NFS_VER3 : NFS_VER2,
	    &sin->sin_port);
	if (error) {
		printf("nfs_boot: portmap NFS, error=%d\n", error);
		return (error);
	}

	/* Construct remote path (for getmntinfo(3)) */
	dp = ndmntp->ndm_host;
	endp = dp + MNAMELEN - 1;
	dp += strlen(dp);
	*dp++ = ':';
	for (sp = pathname; *sp && dp < endp;)
		*dp++ = *sp++;
	*dp = '\0';

	return (0);
}


/*
 * RPC: bootparam/whoami
 * Given client IP address, get:
 *	client name	(hostname)
 *	domain name (domainname)
 *	gateway address
 *
 * The hostname and domainname are set here for convenience.
 *
 * Note - bpsin is initialized to the broadcast address,
 * and will be replaced with the bootparam server address
 * after this call is complete.  Have to use PMAP_PROC_CALL
 * to make sure we get responses only from a servers that
 * know about us (don't want to broadcast a getport call).
 */
static int
bp_whoami(struct sockaddr_in *bpsin, struct in_addr *my_ip,
    struct in_addr *gw_ip)
{
	/* RPC structures for PMAPPROC_CALLIT */
	struct whoami_call {
		u_int32_t call_prog;
		u_int32_t call_vers;
		u_int32_t call_proc;
		u_int32_t call_arglen;
	} *call;
	struct callit_reply {
		u_int32_t port;
		u_int32_t encap_len;
		/* encapsulated data here */
	} *reply;

	struct mbuf *m, *from;
	struct sockaddr_in *sin;
	int error, msg_len;
	int16_t port;

	/*
	 * Build request message for PMAPPROC_CALLIT.
	 */
	m = m_get(M_WAIT, MT_DATA);
	call = mtod(m, struct whoami_call *);
	m->m_len = sizeof(*call);
	call->call_prog = txdr_unsigned(BOOTPARAM_PROG);
	call->call_vers = txdr_unsigned(BOOTPARAM_VERS);
	call->call_proc = txdr_unsigned(BOOTPARAM_WHOAMI);

	/*
	 * append encapsulated data (client IP address)
	 */
	m->m_next = xdr_inaddr_encode(my_ip);
	call->call_arglen = txdr_unsigned(m->m_next->m_len);

	/* RPC: portmap/callit */
	bpsin->sin_port = htons(PMAPPORT);
	from = NULL;
	error = krpc_call(bpsin, PMAPPROG, PMAPVERS,
			PMAPPROC_CALLIT, &m, &from, -1);
	if (error)
		return error;

	/*
	 * Parse result message.
	 */
	if (m->m_len < sizeof(*reply)) {
		m = m_pullup(m, sizeof(*reply));
		if (m == NULL)
			goto bad;
	}
	reply = mtod(m, struct callit_reply *);
	port = fxdr_unsigned(u_int32_t, reply->port);
	msg_len = fxdr_unsigned(u_int32_t, reply->encap_len);
	m_adj(m, sizeof(*reply));

	/*
	 * Save bootparam server address
	 */
	sin = mtod(from, struct sockaddr_in *);
	bpsin->sin_port = htons(port);
	bpsin->sin_addr.s_addr = sin->sin_addr.s_addr;

	/* client name */
	hostnamelen = MAXHOSTNAMELEN-1;
	m = xdr_string_decode(m, hostname, &hostnamelen);
	if (m == NULL)
		goto bad;

	/* domain name */
	domainnamelen = MAXHOSTNAMELEN-1;
	m = xdr_string_decode(m, domainname, &domainnamelen);
	if (m == NULL)
		goto bad;

	/* gateway address */
	m = xdr_inaddr_decode(m, gw_ip);
	if (m == NULL)
		goto bad;

	/* success */
	goto out;

bad:
	printf("nfs_boot: bootparam_whoami: bad reply\n");
	error = EBADRPC;

out:
	m_freem(from);
	m_freem(m);
	return(error);
}


/*
 * RPC: bootparam/getfile
 * Given client name and file "key", get:
 *	server name
 *	server IP address
 *	server pathname
 */
static int
bp_getfile(struct sockaddr_in *bpsin, char *key, struct sockaddr_in *md_sin,
    char *serv_name, char *pathname, int retries)
{
	struct mbuf *m;
	struct sockaddr_in *sin;
	struct in_addr inaddr;
	int error, sn_len, path_len;

	/*
	 * Build request message.
	 */

	/* client name (hostname) */
	m  = xdr_string_encode(hostname, hostnamelen);
	if (m == NULL)
		return (ENOMEM);

	/* key name (root or swap) */
	m->m_next = xdr_string_encode(key, strlen(key));
	if (m->m_next == NULL) {
		m_freem(m);
		return (ENOMEM);
	}

	/* RPC: bootparam/getfile */
	error = krpc_call(bpsin, BOOTPARAM_PROG, BOOTPARAM_VERS,
			BOOTPARAM_GETFILE, &m, NULL, retries);
	if (error)
		return error;

	/*
	 * Parse result message.
	 */

	/* server name */
	sn_len = MNAMELEN-1;
	m = xdr_string_decode(m, serv_name, &sn_len);
	if (m == NULL)
		goto bad;

	/* server IP address (mountd/NFS) */
	m = xdr_inaddr_decode(m, &inaddr);
	if (m == NULL)
		goto bad;

	/* server pathname */
	path_len = MAXPATHLEN-1;
	m = xdr_string_decode(m, pathname, &path_len);
	if (m == NULL)
		goto bad;

	/* setup server socket address */
	sin = md_sin;
	memset(sin, 0, sizeof(*sin));
	sin->sin_len = sizeof(*sin);
	sin->sin_family = AF_INET;
	sin->sin_addr = inaddr;

	/* success */
	goto out;

bad:
	printf("nfs_boot: bootparam_getfile: bad reply\n");
	error = EBADRPC;

out:
	m_freem(m);
	return(error);
}


/*
 * RPC: mountd/mount
 * Given a server pathname, get an NFS file handle.
 * Also, sets sin->sin_port to the NFS service port.
 * mdsin:	mountd server address
 */
static int
md_mount(struct sockaddr_in *mdsin, char *path, struct nfs_args *argp)
{
	/* The RPC structures */
	struct rdata {
		u_int32_t errno;
		union {
			u_int8_t v2fh[NFSX_V2FH];
			struct {
				u_int32_t fhlen;
				u_int8_t fh[1];
			} v3fh;
		} fh;
	} *rdata;
	struct mbuf *m;
	u_int8_t *fh;
	int minlen, error;
	int mntver;

	mntver = (argp->flags & NFSMNT_NFSV3) ? 3 : 2;
	do {
		error = krpc_portmap(mdsin, RPCPROG_MNT, mntver,
		    &mdsin->sin_port);
		if (error)
			continue;

		m = xdr_string_encode(path, strlen(path));
		if (m == NULL)
			return ENOMEM;

		/* Do RPC to mountd. */
		error = krpc_call(mdsin, RPCPROG_MNT, mntver,
		    RPCMNT_MOUNT, &m, NULL, -1);

		if (error != EPROGMISMATCH)
			break;
		/* Try lower version of mountd. */
	} while (--mntver >= 1);
	if (error)
		return error;	/* message already freed */

	if (mntver != 3)
		argp->flags &= ~NFSMNT_NFSV3;

	/* The reply might have only the errno. */
	if (m->m_len < 4)
		goto bad;
	/* Have at least errno, so check that. */
	rdata = mtod(m, struct rdata *);
	error = fxdr_unsigned(u_int32_t, rdata->errno);
	if (error)
		goto out;

	 /* Have errno==0, so the fh must be there. */
	if (mntver == 3) {
		argp->fhsize = fxdr_unsigned(u_int32_t, rdata->fh.v3fh.fhlen);
		if (argp->fhsize > NFSX_V3FHMAX)
			goto bad;
		minlen = 2 * sizeof(u_int32_t) + argp->fhsize;
	} else {
		argp->fhsize = NFSX_V2FH;
		minlen = sizeof(u_int32_t) + argp->fhsize;
	}

	if (m->m_len < minlen) {
		m = m_pullup(m, minlen);
		if (m == NULL)
			return (EBADRPC);
		rdata = mtod(m, struct rdata *);
	}

	fh = (mntver == 3) ? rdata->fh.v3fh.fh : rdata->fh.v2fh;
	bcopy(fh, argp->fh, argp->fhsize);

	goto out;

bad:
	error = EBADRPC;

out:
	m_freem(m);
	return error;
}

#endif /* ifdef NFSCLIENT */
