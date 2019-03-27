/*-
 * SPDX-License-Identifier: BSD-1-Clause
 *
 * Copyright (c) 1990, 1991, 1992, 1993, 1996
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#if 0
#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1990, 1991, 1992, 1993, 1996\n\
The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * rarpd - Reverse ARP Daemon
 *
 * Usage:	rarpd -a [-dfsv] [-t directory] [-P pidfile] [hostname]
 *		rarpd [-dfsv] [-t directory] [-P pidfile] interface [hostname]
 *
 * 'hostname' is optional solely for backwards compatibility with Sun's rarpd.
 * Currently, the argument is ignored.
 */
#include <sys/param.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_types.h>
#include <net/if_dl.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <arpa/inet.h>

#include <dirent.h>
#include <errno.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <stdlib.h>
#include <unistd.h>
#include <libutil.h>

/* Cast a struct sockaddr to a struct sockaddr_in */
#define SATOSIN(sa) ((struct sockaddr_in *)(sa))

#ifndef TFTP_DIR
#define TFTP_DIR "/tftpboot"
#endif

#define ARPSECS (20 * 60)		/* as per code in netinet/if_ether.c */
#define REVARP_REQUEST ARPOP_REVREQUEST
#define REVARP_REPLY ARPOP_REVREPLY

/*
 * The structure for each interface.
 */
struct if_info {
	struct if_info	*ii_next;
	int		ii_fd;			/* BPF file descriptor */
	in_addr_t	ii_ipaddr;		/* IP address */
	in_addr_t	ii_netmask;		/* subnet or net mask */
	u_char		ii_eaddr[ETHER_ADDR_LEN];	/* ethernet address */
	char		ii_ifname[IF_NAMESIZE];
};

/*
 * The list of all interfaces that are being listened to.  rarp_loop()
 * "selects" on the descriptors in this list.
 */
static struct if_info *iflist;

static int verbose;		/* verbose messages */
static const char *tftp_dir = TFTP_DIR;	/* tftp directory */

static int dflag;		/* messages to stdout/stderr, not syslog(3) */
static int sflag;		/* ignore /tftpboot */

static	u_char zero[6];

static char pidfile_buf[PATH_MAX];
static char *pidfile;
#define	RARPD_PIDFILE	"/var/run/rarpd.%s.pid"
static struct pidfh *pidfile_fh;

static int	bpf_open(void);
static in_addr_t	choose_ipaddr(in_addr_t **, in_addr_t, in_addr_t);
static char	*eatoa(u_char *);
static int	expand_syslog_m(const char *fmt, char **newfmt);
static void	init(char *);
static void	init_one(struct ifaddrs *, char *, int);
static char	*intoa(in_addr_t);
static in_addr_t	ipaddrtonetmask(in_addr_t);
static void	logmsg(int, const char *, ...) __printflike(2, 3);
static int	rarp_bootable(in_addr_t);
static int	rarp_check(u_char *, u_int);
static void	rarp_loop(void);
static int	rarp_open(char *);
static void	rarp_process(struct if_info *, u_char *, u_int);
static void	rarp_reply(struct if_info *, struct ether_header *,
		in_addr_t, u_int);
static void	update_arptab(u_char *, in_addr_t);
static void	usage(void);

int
main(int argc, char *argv[])
{
	int op;
	char *ifname, *name;

	int aflag = 0;		/* listen on "all" interfaces  */
	int fflag = 0;		/* don't fork */

	if ((name = strrchr(argv[0], '/')) != NULL)
		++name;
	else
		name = argv[0];
	if (*name == '-')
		++name;

	/*
	 * All error reporting is done through syslog, unless -d is specified
	 */
	openlog(name, LOG_PID | LOG_CONS, LOG_DAEMON);

	opterr = 0;
	while ((op = getopt(argc, argv, "adfsP:t:v")) != -1)
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

		case 's':
			++sflag;
			break;

		case 'P':
			strncpy(pidfile_buf, optarg, sizeof(pidfile_buf) - 1);
			pidfile_buf[sizeof(pidfile_buf) - 1] = '\0';
			pidfile = pidfile_buf;
			break;

		case 't':
			tftp_dir = optarg;
			break;

		case 'v':
			++verbose;
			break;

		default:
			usage();
			/* NOTREACHED */
		}
	argc -= optind;
	argv += optind;

	ifname = (aflag == 0) ? argv[0] : NULL;
	
	if ((aflag && ifname) || (!aflag && ifname == NULL))
		usage();

	init(ifname);

	if (!fflag) {
		if (pidfile == NULL && ifname != NULL && aflag == 0) {
			snprintf(pidfile_buf, sizeof(pidfile_buf) - 1,
			    RARPD_PIDFILE, ifname);
			pidfile_buf[sizeof(pidfile_buf) - 1] = '\0';
			pidfile = pidfile_buf;
		}
		/* If pidfile == NULL, /var/run/<progname>.pid will be used. */
		pidfile_fh = pidfile_open(pidfile, 0600, NULL);
		if (pidfile_fh == NULL)
			logmsg(LOG_ERR, "Cannot open or create pidfile: %s",
			    (pidfile == NULL) ? "/var/run/rarpd.pid" : pidfile);
		if (daemon(0,0)) {
			logmsg(LOG_ERR, "cannot fork");
			pidfile_remove(pidfile_fh);
			exit(1);
		}
		pidfile_write(pidfile_fh);
	}
	rarp_loop();
	return(0);
}

/*
 * Add to the interface list.
 */
static void
init_one(struct ifaddrs *ifa, char *target, int pass1)
{
	struct if_info *ii, *ii2;
	struct sockaddr_dl *ll;
	int family;

	family = ifa->ifa_addr->sa_family;
	switch (family) {
	case AF_INET:
		if (pass1)
			/* Consider only AF_LINK during pass1. */
			return;
		/* FALLTHROUGH */
	case AF_LINK:
		if (!(ifa->ifa_flags & IFF_UP) ||
		    (ifa->ifa_flags & (IFF_LOOPBACK | IFF_POINTOPOINT)))
			return;
		break;
	default:
		return;
	}

	/* Don't bother going any further if not the target interface */
	if (target != NULL && strcmp(ifa->ifa_name, target) != 0)
		return;

	/* Look for interface in list */
	for (ii = iflist; ii != NULL; ii = ii->ii_next)
		if (strcmp(ifa->ifa_name, ii->ii_ifname) == 0)
			break;

	if (pass1 && ii != NULL)
		/* We've already seen that interface once. */
		return;

	/* Allocate a new one if not found */
	if (ii == NULL) {
		ii = (struct if_info *)malloc(sizeof(*ii));
		if (ii == NULL) {
			logmsg(LOG_ERR, "malloc: %m");
			pidfile_remove(pidfile_fh);
			exit(1);
		}
		bzero(ii, sizeof(*ii));
		ii->ii_fd = -1;
		strlcpy(ii->ii_ifname, ifa->ifa_name, sizeof(ii->ii_ifname));
		ii->ii_next = iflist;
		iflist = ii;
	} else if (!pass1 && ii->ii_ipaddr != 0) {
		/*
		 * Second AF_INET definition for that interface: clone
		 * the existing one, and work on that cloned one.
		 * This must be another IP address for this interface,
		 * so avoid killing the previous configuration.
		 */
		ii2 = (struct if_info *)malloc(sizeof(*ii2));
		if (ii2 == NULL) {
			logmsg(LOG_ERR, "malloc: %m");
			pidfile_remove(pidfile_fh);
			exit(1);
		}
		memcpy(ii2, ii, sizeof(*ii2));
		ii2->ii_fd = -1;
		ii2->ii_next = iflist;
		iflist = ii2;

		ii = ii2;
	}

	switch (family) {
	case AF_INET:
		ii->ii_ipaddr = SATOSIN(ifa->ifa_addr)->sin_addr.s_addr;
		ii->ii_netmask = SATOSIN(ifa->ifa_netmask)->sin_addr.s_addr;
		if (ii->ii_netmask == 0)
			ii->ii_netmask = ipaddrtonetmask(ii->ii_ipaddr);
		if (ii->ii_fd < 0)
			ii->ii_fd = rarp_open(ii->ii_ifname);
		break;

	case AF_LINK:
		ll = (struct sockaddr_dl *)ifa->ifa_addr;
		switch (ll->sdl_type) {
		case IFT_ETHER:
		case IFT_L2VLAN:
			bcopy(LLADDR(ll), ii->ii_eaddr, 6);
		}
		break;
	}
}

/*
 * Initialize all "candidate" interfaces that are in the system
 * configuration list.  A "candidate" is up, not loopback and not
 * point to point.
 */
static void
init(char *target)
{
	struct if_info *ii, *nii, *lii;
	struct ifaddrs *ifhead, *ifa;
	int error;

	error = getifaddrs(&ifhead);
	if (error) {
		logmsg(LOG_ERR, "getifaddrs: %m");
		pidfile_remove(pidfile_fh);
		exit(1);
	}
	/*
	 * We make two passes over the list we have got.  In the first
	 * one, we only collect AF_LINK interfaces, and initialize our
	 * list of interfaces from them.  In the second pass, we
	 * collect the actual IP addresses from the AF_INET
	 * interfaces, and allow for the same interface name to appear
	 * multiple times (in case of more than one IP address).
	 */
	for (ifa = ifhead; ifa != NULL; ifa = ifa->ifa_next)
		init_one(ifa, target, 1);
	for (ifa = ifhead; ifa != NULL; ifa = ifa->ifa_next)
		init_one(ifa, target, 0);
	freeifaddrs(ifhead);

	/* Throw away incomplete interfaces */
	lii = NULL;
	for (ii = iflist; ii != NULL; ii = nii) {
		nii = ii->ii_next;
		if (ii->ii_ipaddr == 0 ||
		    bcmp(ii->ii_eaddr, zero, 6) == 0) {
			if (lii == NULL)
				iflist = nii;
			else
				lii->ii_next = nii;
			if (ii->ii_fd >= 0)
				close(ii->ii_fd);
			free(ii);
			continue;
		}
		lii = ii;
	}

	/* Verbose stuff */
	if (verbose)
		for (ii = iflist; ii != NULL; ii = ii->ii_next)
			logmsg(LOG_DEBUG, "%s %s 0x%08x %s",
			    ii->ii_ifname, intoa(ntohl(ii->ii_ipaddr)),
			    (in_addr_t)ntohl(ii->ii_netmask), eatoa(ii->ii_eaddr));
}

static void
usage(void)
{

	(void)fprintf(stderr, "%s\n%s\n",
	    "usage: rarpd -a [-dfsv] [-t directory] [-P pidfile]",
	    "       rarpd [-dfsv] [-t directory] [-P pidfile] interface");
	exit(1);
}

static int
bpf_open(void)
{
	int fd;
	int n = 0;
	char device[sizeof "/dev/bpf000"];

	/*
	 * Go through all the minors and find one that isn't in use.
	 */
	do {
		(void)sprintf(device, "/dev/bpf%d", n++);
		fd = open(device, O_RDWR);
	} while ((fd == -1) && (errno == EBUSY));

	if (fd == -1) {
		logmsg(LOG_ERR, "%s: %m", device);
		pidfile_remove(pidfile_fh);
		exit(1);
	}
	return fd;
}

/*
 * Open a BPF file and attach it to the interface named 'device'.
 * Set immediate mode, and set a filter that accepts only RARP requests.
 */
static int
rarp_open(char *device)
{
	int fd;
	struct ifreq ifr;
	u_int dlt;
	int immediate;

	static struct bpf_insn insns[] = {
		BPF_STMT(BPF_LD|BPF_H|BPF_ABS, 12),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, ETHERTYPE_REVARP, 0, 3),
		BPF_STMT(BPF_LD|BPF_H|BPF_ABS, 20),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, REVARP_REQUEST, 0, 1),
		BPF_STMT(BPF_RET|BPF_K, sizeof(struct ether_arp) +
			 sizeof(struct ether_header)),
		BPF_STMT(BPF_RET|BPF_K, 0),
	};
	static struct bpf_program filter = {
		sizeof insns / sizeof(insns[0]),
		insns
	};

	fd = bpf_open();
	/*
	 * Set immediate mode so packets are processed as they arrive.
	 */
	immediate = 1;
	if (ioctl(fd, BIOCIMMEDIATE, &immediate) == -1) {
		logmsg(LOG_ERR, "BIOCIMMEDIATE: %m");
		goto rarp_open_err;
	}
	strlcpy(ifr.ifr_name, device, sizeof(ifr.ifr_name));
	if (ioctl(fd, BIOCSETIF, (caddr_t)&ifr) == -1) {
		logmsg(LOG_ERR, "BIOCSETIF: %m");
		goto rarp_open_err;
	}
	/*
	 * Check that the data link layer is an Ethernet; this code won't
	 * work with anything else.
	 */
	if (ioctl(fd, BIOCGDLT, (caddr_t)&dlt) == -1) {
		logmsg(LOG_ERR, "BIOCGDLT: %m");
		goto rarp_open_err;
	}
	if (dlt != DLT_EN10MB) {
		logmsg(LOG_ERR, "%s is not an ethernet", device);
		goto rarp_open_err;
	}
	/*
	 * Set filter program.
	 */
	if (ioctl(fd, BIOCSETF, (caddr_t)&filter) == -1) {
		logmsg(LOG_ERR, "BIOCSETF: %m");
		goto rarp_open_err;
	}
	return fd;

rarp_open_err:
	pidfile_remove(pidfile_fh);
	exit(1);
}

/*
 * Perform various sanity checks on the RARP request packet.  Return
 * false on failure and log the reason.
 */
static int
rarp_check(u_char *p, u_int len)
{
	struct ether_header *ep = (struct ether_header *)p;
	struct ether_arp *ap = (struct ether_arp *)(p + sizeof(*ep));

	if (len < sizeof(*ep) + sizeof(*ap)) {
		logmsg(LOG_ERR, "truncated request, got %u, expected %lu",
				len, (u_long)(sizeof(*ep) + sizeof(*ap)));
		return 0;
	}
	/*
	 * XXX This test might be better off broken out...
	 */
	if (ntohs(ep->ether_type) != ETHERTYPE_REVARP ||
	    ntohs(ap->arp_hrd) != ARPHRD_ETHER ||
	    ntohs(ap->arp_op) != REVARP_REQUEST ||
	    ntohs(ap->arp_pro) != ETHERTYPE_IP ||
	    ap->arp_hln != 6 || ap->arp_pln != 4) {
		logmsg(LOG_DEBUG, "request fails sanity check");
		return 0;
	}
	if (bcmp((char *)&ep->ether_shost, (char *)&ap->arp_sha, 6) != 0) {
		logmsg(LOG_DEBUG, "ether/arp sender address mismatch");
		return 0;
	}
	if (bcmp((char *)&ap->arp_sha, (char *)&ap->arp_tha, 6) != 0) {
		logmsg(LOG_DEBUG, "ether/arp target address mismatch");
		return 0;
	}
	return 1;
}

/*
 * Loop indefinitely listening for RARP requests on the
 * interfaces in 'iflist'.
 */
static void
rarp_loop(void)
{
	u_char *buf, *bp, *ep;
	int cc, fd;
	fd_set fds, listeners;
	int bufsize, maxfd = 0;
	struct if_info *ii;

	if (iflist == NULL) {
		logmsg(LOG_ERR, "no interfaces");
		goto rarpd_loop_err;
	}
	if (ioctl(iflist->ii_fd, BIOCGBLEN, (caddr_t)&bufsize) == -1) {
		logmsg(LOG_ERR, "BIOCGBLEN: %m");
		goto rarpd_loop_err;
	}
	buf = malloc(bufsize);
	if (buf == NULL) {
		logmsg(LOG_ERR, "malloc: %m");
		goto rarpd_loop_err;
	}

	while (1) {
		/*
		 * Find the highest numbered file descriptor for select().
		 * Initialize the set of descriptors to listen to.
		 */
		FD_ZERO(&fds);
		for (ii = iflist; ii != NULL; ii = ii->ii_next) {
			FD_SET(ii->ii_fd, &fds);
			if (ii->ii_fd > maxfd)
				maxfd = ii->ii_fd;
		}
		listeners = fds;
		if (select(maxfd + 1, &listeners, NULL, NULL, NULL) == -1) {
			/* Don't choke when we get ptraced */
			if (errno == EINTR)
				continue;
			logmsg(LOG_ERR, "select: %m");
			goto rarpd_loop_err;
		}
		for (ii = iflist; ii != NULL; ii = ii->ii_next) {
			fd = ii->ii_fd;
			if (!FD_ISSET(fd, &listeners))
				continue;
		again:
			cc = read(fd, (char *)buf, bufsize);
			/* Don't choke when we get ptraced */
			if ((cc == -1) && (errno == EINTR))
				goto again;

			/* Loop through the packet(s) */
#define bhp ((struct bpf_hdr *)bp)
			bp = buf;
			ep = bp + cc;
			while (bp < ep) {
				u_int caplen, hdrlen;

				caplen = bhp->bh_caplen;
				hdrlen = bhp->bh_hdrlen;
				if (rarp_check(bp + hdrlen, caplen))
					rarp_process(ii, bp + hdrlen, caplen);
				bp += BPF_WORDALIGN(hdrlen + caplen);
			}
		}
	}
#undef bhp
	return;

rarpd_loop_err:
	pidfile_remove(pidfile_fh);
	exit(1);
}

/*
 * True if this server can boot the host whose IP address is 'addr'.
 * This check is made by looking in the tftp directory for the
 * configuration file.
 */
static int
rarp_bootable(in_addr_t addr)
{
	struct dirent *dent;
	DIR *d;
	char ipname[9];
	static DIR *dd = NULL;

	(void)sprintf(ipname, "%08X", (in_addr_t)ntohl(addr));

	/*
	 * If directory is already open, rewind it.  Otherwise, open it.
	 */
	if ((d = dd) != NULL)
		rewinddir(d);
	else {
		if (chdir(tftp_dir) == -1) {
			logmsg(LOG_ERR, "chdir: %s: %m", tftp_dir);
			goto rarp_bootable_err;
		}
		d = opendir(".");
		if (d == NULL) {
			logmsg(LOG_ERR, "opendir: %m");
			goto rarp_bootable_err;
		}
		dd = d;
	}
	while ((dent = readdir(d)) != NULL)
		if (strncmp(dent->d_name, ipname, 8) == 0)
			return 1;
	return 0;

rarp_bootable_err:
	pidfile_remove(pidfile_fh);
	exit(1);
}

/*
 * Given a list of IP addresses, 'alist', return the first address that
 * is on network 'net'; 'netmask' is a mask indicating the network portion
 * of the address.
 */
static in_addr_t
choose_ipaddr(in_addr_t **alist, in_addr_t net, in_addr_t netmask)
{

	for (; *alist; ++alist)
		if ((**alist & netmask) == net)
			return **alist;
	return 0;
}

/*
 * Answer the RARP request in 'pkt', on the interface 'ii'.  'pkt' has
 * already been checked for validity.  The reply is overlaid on the request.
 */
static void
rarp_process(struct if_info *ii, u_char *pkt, u_int len)
{
	struct ether_header *ep;
	struct hostent *hp;
	in_addr_t target_ipaddr;
	char ename[256];

	ep = (struct ether_header *)pkt;
	/* should this be arp_tha? */
	if (ether_ntohost(ename, (struct ether_addr *)&ep->ether_shost) != 0) {
		logmsg(LOG_ERR, "cannot map %s to name",
			eatoa(ep->ether_shost));
		return;
	}

	if ((hp = gethostbyname(ename)) == NULL) {
		logmsg(LOG_ERR, "cannot map %s to IP address", ename);
		return;
	}

	/*
	 * Choose correct address from list.
	 */
	if (hp->h_addrtype != AF_INET) {
		logmsg(LOG_ERR, "cannot handle non IP addresses for %s",
								ename);
		return;
	}
	target_ipaddr = choose_ipaddr((in_addr_t **)hp->h_addr_list,
				      ii->ii_ipaddr & ii->ii_netmask,
				      ii->ii_netmask);
	if (target_ipaddr == 0) {
		logmsg(LOG_ERR, "cannot find %s on net %s",
		       ename, intoa(ntohl(ii->ii_ipaddr & ii->ii_netmask)));
		return;
	}
	if (sflag || rarp_bootable(target_ipaddr))
		rarp_reply(ii, ep, target_ipaddr, len);
	else if (verbose > 1)
		logmsg(LOG_INFO, "%s %s at %s DENIED (not bootable)",
		    ii->ii_ifname,
		    eatoa(ep->ether_shost),
		    intoa(ntohl(target_ipaddr)));
}

/*
 * Poke the kernel arp tables with the ethernet/ip address combinataion
 * given.  When processing a reply, we must do this so that the booting
 * host (i.e. the guy running rarpd), won't try to ARP for the hardware
 * address of the guy being booted (he cannot answer the ARP).
 */
static struct sockaddr_in sin_inarp = {
	sizeof(struct sockaddr_in), AF_INET, 0,
	{0},
	{0},
};

static struct sockaddr_dl sin_dl = {
	sizeof(struct sockaddr_dl), AF_LINK, 0, IFT_ETHER, 0, 6,
	0, ""
};

static struct {
	struct rt_msghdr rthdr;
	char rtspace[512];
} rtmsg;

static void
update_arptab(u_char *ep, in_addr_t ipaddr)
{
	struct timespec tp;
	int cc;
	struct sockaddr_in *ar, *ar2;
	struct sockaddr_dl *ll, *ll2;
	struct rt_msghdr *rt;
	int xtype, xindex;
	static pid_t pid;
	int r;
	static int seq;

	r = socket(PF_ROUTE, SOCK_RAW, 0);
	if (r == -1) {
		logmsg(LOG_ERR, "raw route socket: %m");
		pidfile_remove(pidfile_fh);
		exit(1);
	}
	pid = getpid();

	ar = &sin_inarp;
	ar->sin_addr.s_addr = ipaddr;
	ll = &sin_dl;
	bcopy(ep, LLADDR(ll), 6);

	/* Get the type and interface index */
	rt = &rtmsg.rthdr;
	bzero(&rtmsg, sizeof(rtmsg));
	rt->rtm_version = RTM_VERSION;
	rt->rtm_addrs = RTA_DST;
	rt->rtm_type = RTM_GET;
	rt->rtm_seq = ++seq;
	ar2 = (struct sockaddr_in *)rtmsg.rtspace;
	bcopy(ar, ar2, sizeof(*ar));
	rt->rtm_msglen = sizeof(*rt) + sizeof(*ar);
	errno = 0;
	if ((write(r, rt, rt->rtm_msglen) == -1) && (errno != ESRCH)) {
		logmsg(LOG_ERR, "rtmsg get write: %m");
		close(r);
		return;
	}
	do {
		cc = read(r, rt, sizeof(rtmsg));
	} while (cc > 0 && (rt->rtm_type != RTM_GET || rt->rtm_seq != seq ||
	    rt->rtm_pid != pid));
	if (cc == -1) {
		logmsg(LOG_ERR, "rtmsg get read: %m");
		close(r);
		return;
	}
	ll2 = (struct sockaddr_dl *)((u_char *)ar2 + ar2->sin_len);
	if (ll2->sdl_family != AF_LINK) {
		/*
		 * XXX I think this means the ip address is not on a
		 * directly connected network (the family is AF_INET in
		 * this case).
		 */
		logmsg(LOG_ERR, "bogus link family (%d) wrong net for %08X?\n",
		    ll2->sdl_family, ipaddr);
		close(r);
		return;
	}
	xtype = ll2->sdl_type;
	xindex = ll2->sdl_index;

	/* Set the new arp entry */
	bzero(rt, sizeof(rtmsg));
	rt->rtm_version = RTM_VERSION;
	rt->rtm_addrs = RTA_DST | RTA_GATEWAY;
	rt->rtm_inits = RTV_EXPIRE;
	clock_gettime(CLOCK_MONOTONIC, &tp);
	rt->rtm_rmx.rmx_expire = tp.tv_sec + ARPSECS;
	rt->rtm_flags = RTF_HOST | RTF_STATIC;
	rt->rtm_type = RTM_ADD;
	rt->rtm_seq = ++seq;

	bcopy(ar, ar2, sizeof(*ar));

	ll2 = (struct sockaddr_dl *)((u_char *)ar2 + sizeof(*ar2));
	bcopy(ll, ll2, sizeof(*ll));
	ll2->sdl_type = xtype;
	ll2->sdl_index = xindex;

	rt->rtm_msglen = sizeof(*rt) + sizeof(*ar2) + sizeof(*ll2);
	errno = 0;
	if ((write(r, rt, rt->rtm_msglen) == -1) && (errno != EEXIST)) {
		logmsg(LOG_ERR, "rtmsg add write: %m");
		close(r);
		return;
	}
	do {
		cc = read(r, rt, sizeof(rtmsg));
	} while (cc > 0 && (rt->rtm_type != RTM_ADD || rt->rtm_seq != seq ||
	    rt->rtm_pid != pid));
	close(r);
	if (cc == -1) {
		logmsg(LOG_ERR, "rtmsg add read: %m");
		return;
	}
}

/*
 * Build a reverse ARP packet and sent it out on the interface.
 * 'ep' points to a valid REVARP_REQUEST.  The REVARP_REPLY is built
 * on top of the request, then written to the network.
 *
 * RFC 903 defines the ether_arp fields as follows.  The following comments
 * are taken (more or less) straight from this document.
 *
 * REVARP_REQUEST
 *
 * arp_sha is the hardware address of the sender of the packet.
 * arp_spa is undefined.
 * arp_tha is the 'target' hardware address.
 *   In the case where the sender wishes to determine his own
 *   protocol address, this, like arp_sha, will be the hardware
 *   address of the sender.
 * arp_tpa is undefined.
 *
 * REVARP_REPLY
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
static void
rarp_reply(struct if_info *ii, struct ether_header *ep, in_addr_t ipaddr,
		u_int len)
{
	u_int n;
	struct ether_arp *ap = (struct ether_arp *)(ep + 1);

	update_arptab((u_char *)&ap->arp_sha, ipaddr);

	/*
	 * Build the rarp reply by modifying the rarp request in place.
	 */
	ap->arp_op = htons(REVARP_REPLY);

#ifdef BROKEN_BPF
	ep->ether_type = ETHERTYPE_REVARP;
#endif
	bcopy((char *)&ap->arp_sha, (char *)&ep->ether_dhost, 6);
	bcopy((char *)ii->ii_eaddr, (char *)&ep->ether_shost, 6);
	bcopy((char *)ii->ii_eaddr, (char *)&ap->arp_sha, 6);

	bcopy((char *)&ipaddr, (char *)ap->arp_tpa, 4);
	/* Target hardware is unchanged. */
	bcopy((char *)&ii->ii_ipaddr, (char *)ap->arp_spa, 4);

	/* Zero possible garbage after packet. */
	bzero((char *)ep + (sizeof(*ep) + sizeof(*ap)),
			len - (sizeof(*ep) + sizeof(*ap)));
	n = write(ii->ii_fd, (char *)ep, len);
	if (n != len)
		logmsg(LOG_ERR, "write: only %d of %d bytes written", n, len);
	if (verbose)
		logmsg(LOG_INFO, "%s %s at %s REPLIED", ii->ii_ifname,
		    eatoa(ap->arp_tha),
		    intoa(ntohl(ipaddr)));
}

/*
 * Get the netmask of an IP address.  This routine is used if
 * SIOCGIFNETMASK doesn't work.
 */
static in_addr_t
ipaddrtonetmask(in_addr_t addr)
{

	addr = ntohl(addr);
	if (IN_CLASSA(addr))
		return htonl(IN_CLASSA_NET);
	if (IN_CLASSB(addr))
		return htonl(IN_CLASSB_NET);
	if (IN_CLASSC(addr))
		return htonl(IN_CLASSC_NET);
	logmsg(LOG_DEBUG, "unknown IP address class: %08X", addr);
	return htonl(0xffffffff);
}

/*
 * A faster replacement for inet_ntoa().
 */
static char *
intoa(in_addr_t addr)
{
	char *cp;
	u_int byte;
	int n;
	static char buf[sizeof(".xxx.xxx.xxx.xxx")];

	cp = &buf[sizeof buf];
	*--cp = '\0';

	n = 4;
	do {
		byte = addr & 0xff;
		*--cp = byte % 10 + '0';
		byte /= 10;
		if (byte > 0) {
			*--cp = byte % 10 + '0';
			byte /= 10;
			if (byte > 0)
				*--cp = byte + '0';
		}
		*--cp = '.';
		addr >>= 8;
	} while (--n > 0);

	return cp + 1;
}

static char *
eatoa(u_char *ea)
{
	static char buf[sizeof("xx:xx:xx:xx:xx:xx")];

	(void)sprintf(buf, "%x:%x:%x:%x:%x:%x",
	    ea[0], ea[1], ea[2], ea[3], ea[4], ea[5]);
	return (buf);
}

static void
logmsg(int pri, const char *fmt, ...)
{
	va_list v;
	FILE *fp;
	char *newfmt;

	va_start(v, fmt);
	if (dflag) {
		if (pri == LOG_ERR)
			fp = stderr;
		else
			fp = stdout;
		if (expand_syslog_m(fmt, &newfmt) == -1) {
			vfprintf(fp, fmt, v);
		} else {
			vfprintf(fp, newfmt, v);
			free(newfmt);
		}
		fputs("\n", fp);
		fflush(fp);
	} else {
		vsyslog(pri, fmt, v);
	}
	va_end(v);
}

static int
expand_syslog_m(const char *fmt, char **newfmt) {
	const char *str, *m;
	char *p, *np;

	p = strdup("");
	str = fmt;
	while ((m = strstr(str, "%m")) != NULL) {
		asprintf(&np, "%s%.*s%s", p, (int)(m - str),
		    str, strerror(errno));
		free(p);
		if (np == NULL) {
			errno = ENOMEM;
			return (-1);
		}
		p = np;
		str = m + 2;
	}
	
	if (*str != '\0') {
		asprintf(&np, "%s%s", p, str);
		free(p);
		if (np == NULL) {
			errno = ENOMEM;
			return (-1);
		}
		p = np;
	}

	*newfmt = p;
	return (0);
}
