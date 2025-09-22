/*	$OpenBSD: rarpd.c,v 1.80 2022/10/04 07:01:38 kn Exp $ */
/*	$NetBSD: rarpd.c,v 1.25 1998/04/23 02:48:33 mrg Exp $	*/

/*
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

/*
 * rarpd - Reverse ARP Daemon
 */

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/bpf.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <poll.h>
#include <ifaddrs.h>

/*
 * The structures for each interface.
 */
struct if_addr {
	in_addr_t ia_ipaddr;		/* IP address of this interface */
	in_addr_t ia_netmask;		/* subnet or net mask */
	struct if_addr *ia_next;
};

struct if_info {
	int	ii_fd;			/* BPF file descriptor */
	char	ii_name[IFNAMSIZ];	/* if name, e.g. "en0" */
	u_char	ii_eaddr[ETHER_ADDR_LEN];	/* Ethernet address of this iface */
	struct if_addr *ii_addrs;	/* Networks this interface is on */
	struct if_info *ii_next;
};
/*
 * The list of all interfaces that are being listened to.  rarp_loop()
 * "selects" on the descriptors in this list.
 */
struct if_info *iflist;

int    rarp_open(char *);
void   init_one(char *);
void   init_all(void);
void   rarp_loop(void);
void   lookup_addrs(char *, struct if_info *);
__dead void   usage(void);
void   rarp_process(struct if_info *, u_char *);
void   rarp_reply(struct if_info *, struct if_addr *,
	    struct ether_header *, u_int32_t, struct hostent *);
void	arptab_init(void);
int    arptab_set(u_char *, u_int32_t);
__dead void   error(const char *, ...);
void   warning(const char *, ...);
void   debug(const char *, ...);
u_int32_t ipaddrtonetmask(u_int32_t);
int    rarp_bootable(u_int32_t);

int	aflag = 0;		/* listen on "all" interfaces  */
int	dflag = 0;		/* print debugging messages */
int	fflag = 0;		/* don't fork */
int	lflag = 0;		/* log all replies */
int	tflag = 0;		/* tftpboot check */

#ifndef TFTP_DIR
#define TFTP_DIR "/tftpboot"
#endif

int
main(int argc, char *argv[])
{
	extern char *__progname;
	int op;

	/* All error reporting is done through syslogs. */
	openlog(__progname, LOG_PID | LOG_CONS, LOG_DAEMON);

	opterr = 0;
	while ((op = getopt(argc, argv, "adflt")) != -1) {
		switch (op) {
		case 'a':
			++aflag;
			break;
		case 'd':
			++dflag;
			break;
		case 'f':
			++fflag;
			break;
		case 'l':
			++lflag;
			break;
		case 't':
			++tflag;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if ((aflag && argc > 0) || (!aflag && argc == 0))
		usage();

	if (aflag)
		init_all();
	else
		while (argc > 0) {
			init_one(argv[0]);
			argc--;
			argv++;
		}

	if ((!fflag) && (!dflag)) {
		if (daemon(0, 0) == -1)
			error("failed to daemonize: %s", strerror(errno));
	}
	rarp_loop();
	exit(0);
}

/*
 * Add 'ifname' to the interface list.  Lookup its IP address and network
 * mask and Ethernet address, and open a BPF file for it.
 */
void
init_one(char *ifname)
{
	struct if_info *p;
	int fd;

	/* first check to see if this "if" was already opened? */
	for (p = iflist; p; p = p->ii_next)
		if (!strncmp(p->ii_name, ifname, IFNAMSIZ))
			return;

	fd = rarp_open(ifname);
	if (fd < 0)
		return;

	p = malloc(sizeof(*p));
	if (p == 0)
		error("malloc: %s", strerror(errno));

	p->ii_next = iflist;
	iflist = p;

	p->ii_fd = fd;
	strncpy(p->ii_name, ifname, IFNAMSIZ);
	p->ii_addrs = NULL;
	lookup_addrs(ifname, p);
}
/*
 * Initialize all "candidate" interfaces that are in the system
 * configuration list.  A "candidate" is up, not loopback and not
 * point to point.
 */
void
init_all(void)
{
	struct ifaddrs *ifap, *ifa;
	struct sockaddr_dl *sdl;

	if (getifaddrs(&ifap) != 0)
		error("getifaddrs: %s", strerror(errno));

	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL)
			continue;
		sdl = (struct sockaddr_dl *)ifa->ifa_addr;
		if (sdl->sdl_family != AF_LINK || sdl->sdl_type != IFT_ETHER ||
		    sdl->sdl_alen != 6)
			continue;

		if ((ifa->ifa_flags &
		    (IFF_UP | IFF_LOOPBACK | IFF_POINTOPOINT)) != IFF_UP)
			continue;
		init_one(ifa->ifa_name);
	}
	freeifaddrs(ifap);
}

__dead void
usage(void)
{
	(void) fprintf(stderr, "usage: rarpd [-adflt] if0 [... ifN]\n");
	exit(1);
}

static struct bpf_insn insns[] = {
	BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 12),
	BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, ETHERTYPE_REVARP, 0, 3),
	BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 20),
	BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, ARPOP_REVREQUEST, 0, 1),
	BPF_STMT(BPF_RET | BPF_K, sizeof(struct ether_arp) +
	    sizeof(struct ether_header)),
	BPF_STMT(BPF_RET | BPF_K, 0),
};

static struct bpf_program filter = {
	sizeof insns / sizeof(insns[0]),
	insns
};

/*
 * Open a BPF file and attach it to the interface named 'device'.
 * Set immediate mode, and set a filter that accepts only RARP requests.
 */
int
rarp_open(char *device)
{
	int	fd, immediate;
	struct ifreq ifr;
	u_int   dlt;

	if ((fd = open("/dev/bpf", O_RDWR)) == -1)
		error("/dev/bpf: %s", strerror(errno));

	/* Set immediate mode so packets are processed as they arrive. */
	immediate = 1;
	if (ioctl(fd, BIOCIMMEDIATE, &immediate) == -1) {
		error("BIOCIMMEDIATE: %s", strerror(errno));
	}

	(void) strncpy(ifr.ifr_name, device, sizeof ifr.ifr_name);
	if (ioctl(fd, BIOCSETIF, (caddr_t)&ifr) == -1) {
		if (aflag) {	/* for -a skip not ethernet interfaces */
			close(fd);
			return -1;
		}
		error("BIOCSETIF: %s", strerror(errno));
	}

	/*
	 * Check that the data link layer is an Ethernet; this code
	 * won't work with anything else.
	 */
	if (ioctl(fd, BIOCGDLT, (caddr_t) &dlt) == -1)
		error("BIOCGDLT: %s", strerror(errno));
	if (dlt != DLT_EN10MB) {
		if (aflag) {	/* for -a skip not ethernet interfaces */
			close(fd);
			return -1;
		}
		error("%s is not an ethernet", device);
	}
	/* Set filter program. */
	if (ioctl(fd, BIOCSETF, (caddr_t)&filter) == -1)
		error("BIOCSETF: %s", strerror(errno));
	return fd;
}
/*
 * Perform various sanity checks on the RARP request packet.  Return
 * false on failure and log the reason.
 */
static int
rarp_check(u_char *p, int len)
{
	struct ether_header *ep = (struct ether_header *) p;
	struct ether_arp *ap = (struct ether_arp *) (p + sizeof(*ep));

	(void) debug("got a packet");

	if (len < sizeof(*ep) + sizeof(*ap)) {
		warning("truncated request");
		return 0;
	}
	/* XXX This test might be better off broken out... */
	if (ntohs (ep->ether_type) != ETHERTYPE_REVARP ||
	    ntohs (ap->arp_hrd) != ARPHRD_ETHER ||
	    ntohs (ap->arp_op) != ARPOP_REVREQUEST ||
	    ntohs (ap->arp_pro) != ETHERTYPE_IP ||
	    ap->arp_hln != 6 || ap->arp_pln != 4) {
		warning("request fails sanity check");
		return 0;
	}
	if (memcmp((char *) &ep->ether_shost, (char *) &ap->arp_sha, 6) != 0) {
		warning("ether/arp sender address mismatch");
		return 0;
	}
	if (memcmp((char *) &ap->arp_sha, (char *) &ap->arp_tha, 6) != 0) {
		warning("ether/arp target address mismatch");
		return 0;
	}
	return 1;
}

/*
 * Loop indefinitely listening for RARP requests on the
 * interfaces in 'iflist'.
 */
void
rarp_loop(void)
{
	int	cc, fd, numfd = 0, i;
	u_int	bufsize;
	struct pollfd *pfd;
	u_char	*buf, *bp, *ep;
	struct if_info *ii;

	if (iflist == 0)
		error("no interfaces");
	if (ioctl(iflist->ii_fd, BIOCGBLEN, (caddr_t)&bufsize) == -1)
		error("BIOCGBLEN: %s", strerror(errno));

	arptab_init();

	if (tflag)
		if (unveil(TFTP_DIR, "r") == -1)
			error("unveil %s", TFTP_DIR);
	if (unveil("/etc/ethers", "r") == -1)
		error("unveil /etc/ethers");
	if (pledge("stdio rpath dns", NULL) == -1)
		error("pledge");

	buf = malloc((size_t) bufsize);
	if (buf == 0)
		error("malloc: %s", strerror(errno));
	/*
	 * Initialize the set of descriptors to listen to.
	 */
	for (ii = iflist; ii; ii = ii->ii_next)
		numfd++;
	pfd = reallocarray(NULL, numfd, sizeof(*pfd));
	if (pfd == NULL)
		error("reallocarray: %s", strerror(errno));
	for (i = 0, ii = iflist; ii; ii = ii->ii_next, i++) {
		pfd[i].fd = ii->ii_fd;
		pfd[i].events = POLLIN;
	}

	while (1) {
		if (poll(pfd, numfd, -1) == -1) {
			if (errno == EINTR)
				continue;
			error("poll: %s", strerror(errno));
		}
		for (i = 0, ii = iflist; ii; ii = ii->ii_next, i++) {
			if (pfd[i].revents == 0)
				continue;
			fd = ii->ii_fd;
		again:
			cc = read(fd, (char *)buf, bufsize);
			/* Don't choke when we get ptraced */
			if (cc == -1 && errno == EINTR)
				goto again;
			if (cc == -1)
				error("read: %s", strerror(errno));
			/* Loop through the packet(s) */
#define bhp ((struct bpf_hdr *)bp)
			bp = buf;
			ep = bp + cc;
			while (bp < ep) {
				int caplen, hdrlen;

				caplen = bhp->bh_caplen;
				hdrlen = bhp->bh_hdrlen;
				if (rarp_check(bp + hdrlen, caplen))
					rarp_process(ii, bp + hdrlen);
				bp += BPF_WORDALIGN(hdrlen + caplen);
			}
		}
	}
	free(pfd);
}

/*
 * True if this server can boot the host whose IP address is 'addr'.
 * This check is made by looking in the tftp directory for the
 * configuration file.
 */
int
rarp_bootable(u_int32_t addr)
{
	struct dirent *dent;
	char    ipname[40];
	static DIR *dd = 0;
	DIR *d;

	(void) snprintf(ipname, sizeof ipname, "%08X", addr);
	/* If directory is already open, rewind it.  Otherwise, open it. */
	if ((d = dd))
		rewinddir(d);
	else {
		if (chdir(TFTP_DIR) == -1)
			error("chdir: %s", strerror(errno));
		d = opendir(".");
		if (d == 0)
			error("opendir: %s", strerror(errno));
		dd = d;
	}
	while ((dent = readdir(d)))
		if (strncmp(dent->d_name, ipname, 8) == 0)
			return 1;
	return 0;
}


/*
 * Given a list of IP addresses, 'alist', return the first address that
 * is on network 'net'; 'netmask' is a mask indicating the network portion
 * of the address.
 */
static u_int32_t
choose_ipaddr(u_int32_t **alist, u_int32_t net, u_int32_t netmask)
{
	for (; *alist; ++alist) {
		if ((**alist & netmask) == net)
			return **alist;
	}
	return 0;
}
/*
 * Answer the RARP request in 'pkt', on the interface 'ii'.  'pkt' has
 * already been checked for validity.  The reply is overlaid on the request.
 */
void
rarp_process(struct if_info *ii, u_char *pkt)
{
	char    ename[HOST_NAME_MAX+1];
	u_int32_t  target_ipaddr;
	struct ether_header *ep;
	struct ether_addr *ea;
	struct hostent *hp;
	struct	in_addr in;
	struct if_addr *ia;

	ep = (struct ether_header *) pkt;
	ea = (struct ether_addr *) &ep->ether_shost;

	debug("%s", ether_ntoa(ea));
	if (ether_ntohost(ename, ea) != 0) {
		debug("ether_ntohost failed");
		return;
	}
	if ((hp = gethostbyname(ename)) == 0) {
		debug("gethostbyname (%s) failed", ename);
		return;
	}

	/* Choose correct address from list. */
	if (hp->h_addrtype != AF_INET)
		error("cannot handle non IP addresses");
	for (target_ipaddr = 0, ia = ii->ii_addrs; ia; ia = ia->ia_next) {
		target_ipaddr = choose_ipaddr((u_int32_t **) hp->h_addr_list,
		    ia->ia_ipaddr & ia->ia_netmask, ia->ia_netmask);
		if (target_ipaddr)
			break;
	}

	if (target_ipaddr == 0) {
		for (ia = ii->ii_addrs; ia; ia = ia->ia_next) {
			in.s_addr = ia->ia_ipaddr & ia->ia_netmask;
			warning("cannot find %s on net %s",
			    ename, inet_ntoa(in));
		}
		return;
	}
	if (tflag == 0 || rarp_bootable(htonl(target_ipaddr)))
		rarp_reply(ii, ia, ep, target_ipaddr, hp);
	debug("reply sent");
}

/*
 * Lookup the ethernet address of the interface attached to the BPF
 * file descriptor 'fd'; return it in 'eaddr'.
 */
void
lookup_addrs(char *ifname, struct if_info *p)
{
	struct ifaddrs *ifap, *ifa;
	struct sockaddr_dl *sdl;
	u_char *eaddr = p->ii_eaddr;
	struct if_addr *ia, **iap = &p->ii_addrs;
	struct in_addr in;
	int found = 0;

	if (getifaddrs(&ifap) != 0)
		error("getifaddrs: %s", strerror(errno));

	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		if (strcmp(ifa->ifa_name, ifname))
			continue;
		if (ifa->ifa_addr == NULL)
			continue;
		sdl = (struct sockaddr_dl *) ifa->ifa_addr;
		if (sdl->sdl_family == AF_LINK &&
		    sdl->sdl_type == IFT_ETHER && sdl->sdl_alen == 6) {
			memcpy((caddr_t)eaddr, (caddr_t)LLADDR(sdl), 6);
			if (dflag)
				fprintf(stderr, "%s: %x:%x:%x:%x:%x:%x\n",
				    ifa->ifa_name,
				    eaddr[0], eaddr[1], eaddr[2],
				    eaddr[3], eaddr[4], eaddr[5]);
			found = 1;
		} else if (sdl->sdl_family == AF_INET) {
			ia = malloc(sizeof (struct if_addr));
			if (ia == NULL)
				error("lookup_addrs: malloc: %s",
				    strerror(errno));
			ia->ia_next = NULL;
			ia->ia_ipaddr =
			    ((struct sockaddr_in *) ifa->ifa_addr)->
			    sin_addr.s_addr;
			ia->ia_netmask =
			    ((struct sockaddr_in *) ifa->ifa_netmask)->
			    sin_addr.s_addr;
			/* Figure out a mask from the IP address class. */
			if (ia->ia_netmask == 0)
				ia->ia_netmask =
				    ipaddrtonetmask(ia->ia_ipaddr);
			if (dflag) {
				in.s_addr = ia->ia_ipaddr;
				fprintf(stderr, "\t%s\n",
				    inet_ntoa(in));
			}
			*iap = ia;
			iap = &ia->ia_next;
		}
	}
	freeifaddrs(ifap);
	if (!found)
		error("lookup_addrs: Never saw interface `%s'!", ifname);
}

/*
 * Build a reverse ARP packet and sent it out on the interface.
 * 'ep' points to a valid ARPOP_REVREQUEST.  The ARPOP_REVREPLY is built
 * on top of the request, then written to the network.
 *
 * RFC 903 defines the ether_arp fields as follows.  The following comments
 * are taken (more or less) straight from this document.
 *
 * ARPOP_REVREQUEST
 *
 * arp_sha is the hardware address of the sender of the packet.
 * arp_spa is undefined.
 * arp_tha is the 'target' hardware address.
 *   In the case where the sender wishes to determine his own
 *   protocol address, this, like arp_sha, will be the hardware
 *   address of the sender.
 * arp_tpa is undefined.
 *
 * ARPOP_REVREPLY
 *
 * arp_sha is the hardware address of the responder (the sender of the
 *   reply packet).
 * arp_spa is the protocol address of the responder (see the note below).
 * arp_tha is the hardware address of the target, and should be the same as
 *   that which was given in the request.
 * arp_tpa is the protocol address of the target, that is, the desired address.
 *
 * Note that the requirement that arp_spa be filled in with the responder's
 * protocol is purely for convenience.  For instance, if a system were to use
 * both ARP and RARP, then the inclusion of the valid protocol-hardware
 * address pair (arp_spa, arp_sha) may eliminate the need for a subsequent
 * ARP request.
 */
void
rarp_reply(struct if_info *ii, struct if_addr *ia, struct ether_header *ep,
    u_int32_t ipaddr, struct hostent *hp)
{
	struct ether_arp *ap = (struct ether_arp *) (ep + 1);
	int len, n;

	/*
	 * Poke the kernel arp tables with the ethernet/ip address
	 * combination given.  When processing a reply, we must
	 * do this so that the booting host (i.e. the guy running
	 * rarpd), won't try to ARP for the hardware address of the
	 * guy being booted (he cannot answer the ARP).
	 */
	if (arptab_set((u_char *)&ap->arp_sha, ipaddr) > 0)
		syslog(LOG_ERR, "couldn't update arp table");

	/* Build the rarp reply by modifying the rarp request in place. */
	ep->ether_type = htons(ETHERTYPE_REVARP);
	ap->ea_hdr.ar_hrd = htons(ARPHRD_ETHER);
	ap->ea_hdr.ar_pro = htons(ETHERTYPE_IP);
	ap->arp_op = htons(ARPOP_REVREPLY);

	memcpy((char *) &ep->ether_dhost, (char *) &ap->arp_sha, 6);
	memcpy((char *) &ep->ether_shost, (char *) ii->ii_eaddr, 6);
	memcpy((char *) &ap->arp_sha, (char *) ii->ii_eaddr, 6);

	memcpy((char *) ap->arp_tpa, (char *) &ipaddr, 4);
	/* Target hardware is unchanged. */
	memcpy((char *) ap->arp_spa, (char *) &ia->ia_ipaddr, 4);

	if (lflag) {
		struct ether_addr ea;

		memcpy(&ea.ether_addr_octet, &ap->arp_sha, 6);
		syslog(LOG_INFO, "%s asked; %s replied", hp->h_name,
		    ether_ntoa(&ea));
	}

	len = sizeof(*ep) + sizeof(*ap);
	n = write(ii->ii_fd, (char *) ep, len);
	if (n != len)
		warning("write: only %d of %d bytes written", n, len);
}
/*
 * Get the netmask of an IP address.
 */
u_int32_t
ipaddrtonetmask(u_int32_t addr)
{
	if (IN_CLASSA(addr))
		return IN_CLASSA_NET;
	if (IN_CLASSB(addr))
		return IN_CLASSB_NET;
	if (IN_CLASSC(addr))
		return IN_CLASSC_NET;
	error("unknown IP address class: %08X", addr);
}

void
warning(const char *fmt,...)
{
	va_list ap;

	if (dflag) {
		(void) fprintf(stderr, "rarpd: warning: ");
		va_start(ap, fmt);
		(void) vfprintf(stderr, fmt, ap);
		va_end(ap);
		(void) fprintf(stderr, "\n");
	}
	va_start(ap, fmt);
	vsyslog(LOG_ERR, fmt, ap);
	va_end(ap);
}

__dead void
error(const char *fmt,...)
{
	va_list ap;

	if (dflag) {
		(void) fprintf(stderr, "rarpd: error: ");
		va_start(ap, fmt);
		(void) vfprintf(stderr, fmt, ap);
		va_end(ap);
		(void) fprintf(stderr, "\n");
	}
	va_start(ap, fmt);
	vsyslog(LOG_ERR, fmt, ap);
	va_end(ap);
	exit(1);
}

void
debug(const char *fmt,...)
{
	va_list ap;

	if (dflag) {
		va_start(ap, fmt);
		(void) fprintf(stderr, "rarpd: ");
		(void) vfprintf(stderr, fmt, ap);
		va_end(ap);
		(void) fprintf(stderr, "\n");
	}
}
