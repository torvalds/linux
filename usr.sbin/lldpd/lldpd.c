/*	$OpenBSD: lldpd.c,v 1.9 2025/05/16 04:04:41 kn Exp $ */

/*
 * Copyright (c) 2024 David Gwynne <dlg@openbsd.org>
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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/un.h>
#include <sys/stat.h>

#include <sys/queue.h>
#include <sys/tree.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <net/frame.h>
#include <net/if_types.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <net/ethertypes.h>
#include <net/lldp.h>

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>
#include <ifaddrs.h>
#include <pwd.h>
#include <paths.h>

#include <event.h>

#include "pdu.h"
#include "lldpctl.h"

#include <syslog.h>
#include "log.h"

#ifndef nitems
#define nitems(_a) ((sizeof((_a)) / sizeof((_a)[0])))
#endif

int rdaemon(int);

#define LLDPD_USER		"_lldpd"

#define CMSG_FOREACH(_cmsg, _msgp) \
	for ((_cmsg) = CMSG_FIRSTHDR((_msgp)); \
	    (_cmsg) != NULL; \
	    (_cmsg) = CMSG_NXTHDR((_msgp), (_cmsg)))

static const uint8_t maddr[ETHER_ADDR_LEN] =
    { 0x01, 0x80, 0xc2, 0x00, 0x00, 0x0e };

static inline int
cmsg_match(const struct cmsghdr *cmsg, size_t len, int level, int type)
{
	return (cmsg->cmsg_len == CMSG_LEN(len) &&
	    cmsg->cmsg_level == level && cmsg->cmsg_type == type);
}

#define CMSG_MATCH(_cmsg, _len, _level, _type) \
	cmsg_match((_cmsg), (_len), (_level), (_type))

struct iface;

struct lldp_msap {
	struct iface		*msap_iface;
	struct ether_addr	 msap_saddr;
	struct ether_addr	 msap_daddr;
	struct timespec		 msap_created;
	struct timespec		 msap_updated;
	uint64_t		 msap_packets;
	uint64_t		 msap_updates;

	struct event		 msap_expiry;

	TAILQ_ENTRY(lldp_msap)	 msap_entry;	/* iface list */
	TAILQ_ENTRY(lldp_msap)	 msap_aentry;	/* daemon list */
	unsigned int		 msap_refs;

	void *			*msap_pdu;
	size_t			 msap_pdu_len;

	/*
         * LLDP PDUs are required to start with chassis id, port
	 * id, and ttl as the first three TLVs. The MSAP id is made
	 * up of the chassis and port id, so rathert than extract and
	 * compose an id out of these TLVs, we can use the start of
	 * the PDU directly as the identifier.
	 */
	unsigned int		 msap_id_len;
};

TAILQ_HEAD(lldp_msaps, lldp_msap);

struct iface_key {
	unsigned int		 if_index;
	char			 if_name[IFNAMSIZ];
};

struct iface {
	struct iface_key	 if_key; /* must be first */
	RBT_ENTRY(iface)	 if_entry;

	struct lldpd		*if_lldpd;
	struct lldp_msaps	 if_msaps;

	uint64_t		 if_agent_counters[AGENT_COUNTER_NCOUNTERS];
};

RBT_HEAD(ifaces, iface);

static inline int

iface_cmp(const struct iface *a, const struct iface *b)
{
	const struct iface_key *ka = &a->if_key;
	const struct iface_key *kb = &b->if_key;

	if (ka->if_index > kb->if_index)
		return (1);
	if (ka->if_index < kb->if_index)
		return (-1);
	return (0);
}

RBT_PROTOTYPE(ifaces, iface, if_entry, iface_cmp);

struct lldpd_ctl {
	struct lldpd		*ctl_lldpd;

	struct event		 ctl_rd_ev;
	struct event		 ctl_wr_ev;

	uid_t			 ctl_peer_uid;
	gid_t			 ctl_peer_gid;

	void (*ctl_handler)(struct lldpd *, struct lldpd_ctl *, int fd);
	void			*ctl_ctx;
};

struct lldpd {
	const char		*ctl_path;

	struct event		 rt_ev;
	struct event		 en_ev;
	struct event		 ctl_ev;
	int			 s;

	struct ifaces		 ifaces;

	struct lldp_msaps	 msaps;

	uint64_t		 agent_counters[AGENT_COUNTER_NCOUNTERS];
};

static void	rtsock_open(struct lldpd *);
static void	rtsock_recv(int, short, void *);
static void	ensock_open(struct lldpd *);
static void	ensock_recv(int, short, void *);
static void	ctlsock_open(struct lldpd *);
static void	ctlsock_accept(int, short, void *);

static int	getall(struct lldpd *);

extern char *__progname;

__dead static void
usage(void)
{
	fprintf(stderr, "usage: %s [-d] [-s socket]\n", __progname);
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct lldpd _lldpd = {
		.ctl_path = LLDP_CTL_PATH,
		.ifaces = RBT_INITIALIZER(_lldpd.ifaces),
		.msaps = TAILQ_HEAD_INITIALIZER(_lldpd.msaps),
	};
	struct lldpd *lldpd = &_lldpd; /* let me use -> consistently */
	struct passwd *pw;
	int debug = 0;
	int devnull = -1;

	int ch;

	while ((ch = getopt(argc, argv, "ds:")) != -1) {
		switch (ch) {
		case 'd':
			debug = 1;
			break;
		case 's':
			lldpd->ctl_path = optarg;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 0)
		usage();

	if (geteuid() != 0)
		errx(1, "need root privileges");

	closefrom(STDERR_FILENO + 1);
	pw = getpwnam(LLDPD_USER);
	if (pw == NULL)
		errx(1, "no %s user", LLDPD_USER);

	if (!debug) {
		logger_syslog(__progname, LOG_DAEMON);
		devnull = open(_PATH_DEVNULL, O_RDWR);
		if (devnull == -1)
			err(1, "%s", _PATH_DEVNULL);
	}

	rtsock_open(lldpd);
	ensock_open(lldpd);
	ctlsock_open(lldpd);

	if (chroot(pw->pw_dir) == -1)
		err(1, "chroot %s", pw->pw_dir);
	if (chdir("/") == -1)
		err(1, "chdir %s", pw->pw_dir);

	/* drop privs */
	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		errx(1, "can't drop privileges");

	lldpd->s = socket(AF_INET, SOCK_DGRAM, 0);
	if (lldpd->s == -1)
		err(1, "inet sock");

	if (getall(lldpd) == -1)
		warn("getall");

	if (!debug && rdaemon(devnull) == -1)
		err(1, "unable to daemonize");

	if (pledge("stdio unix", NULL) == -1)
		err(1, "pledge");

	event_init();

	event_set(&lldpd->rt_ev, EVENT_FD(&lldpd->rt_ev),
	    EV_READ|EV_PERSIST, rtsock_recv, lldpd);
	event_set(&lldpd->en_ev, EVENT_FD(&lldpd->en_ev),
	    EV_READ|EV_PERSIST, ensock_recv, lldpd);
	event_set(&lldpd->ctl_ev, EVENT_FD(&lldpd->ctl_ev),
	    EV_READ|EV_PERSIST, ctlsock_accept, lldpd);

	event_add(&lldpd->rt_ev, NULL);
	event_add(&lldpd->en_ev, NULL);
	event_add(&lldpd->ctl_ev, NULL);

	event_dispatch();

	return (0);
}

static void
agent_counter_inc(struct lldpd *lldpd, struct iface *ifp,
    enum agent_counter c)
{
	lldpd->agent_counters[c]++;
	ifp->if_agent_counters[c]++;
}

static struct lldp_msap *
lldp_msap_take(struct lldpd *lldpd, struct lldp_msap *msap)
{
	msap->msap_refs++;
	return (msap);
}

static void
lldp_msap_rele(struct lldpd *lldpd, struct lldp_msap *msap)
{
	if (--msap->msap_refs == 0) {
		TAILQ_REMOVE(&lldpd->msaps, msap, msap_aentry);
		free(msap->msap_pdu);
		free(msap);
	}
}

static void
lldp_msap_remove(struct iface *ifp, struct lldp_msap *msap)
{
	evtimer_del(&msap->msap_expiry);
	TAILQ_REMOVE(&ifp->if_msaps, msap, msap_entry);

	lldp_msap_rele(ifp->if_lldpd, msap);
}

static void
lldp_msap_expire(int nil, short events, void *arg)
{
	struct lldp_msap *msap = arg;
	struct iface *ifp = msap->msap_iface;
	struct lldpd *lldpd = ifp->if_lldpd;

	agent_counter_inc(lldpd, ifp, statsAgeoutsTotal);

	ldebug("%s: entry from %s has expired", ifp->if_key.if_name,
	    ether_ntoa(&msap->msap_saddr));

	lldp_msap_remove(ifp, msap);
}

static void
rtsock_open(struct lldpd *lldpd)
{
	unsigned int rtfilter;
	int s;

	s = socket(AF_ROUTE, SOCK_RAW | SOCK_NONBLOCK, AF_UNSPEC);
	if (s == -1)
		err(1, "route socket");

	rtfilter = ROUTE_FILTER(RTM_IFINFO) | ROUTE_FILTER(RTM_IFANNOUNCE);
	if (setsockopt(s, AF_ROUTE, ROUTE_MSGFILTER,
	    &rtfilter, sizeof(rtfilter)) == -1)
		err(1, "route socket setsockopt msgfilter");

	event_set(&lldpd->rt_ev, s, 0, NULL, NULL);
}

static inline struct iface *
iface_insert(struct lldpd *lldpd, struct iface *ifp)
{
	return (RBT_INSERT(ifaces, &lldpd->ifaces, ifp));
}

static struct iface *
iface_find(struct lldpd *lldpd, const char *ifname, int ifindex)
{
	struct iface_key key = { .if_index = ifindex };

	return (RBT_FIND(ifaces, &lldpd->ifaces, (struct iface *)&key));
}

static inline void
iface_remove(struct lldpd *lldpd, struct iface *ifp)
{
	RBT_REMOVE(ifaces, &lldpd->ifaces, ifp);
}

static void
rtsock_if_attach(struct lldpd *lldpd, const struct if_announcemsghdr *ifan)
{
	struct ifreq ifr;
	struct if_data ifi;
	struct iface *ifp;
	struct frame_mreq fmr;

	memset(&ifr, 0, sizeof(ifr));
	memcpy(ifr.ifr_name, ifan->ifan_name, sizeof(ifr.ifr_name));
	ifr.ifr_data = (caddr_t)&ifi;

	if (ioctl(lldpd->s, SIOCGIFDATA, &ifr) == -1) {
		lwarn("%s index %u: attach get data",
		    ifan->ifan_name, ifan->ifan_index);
		return;
	}

	if (ifi.ifi_type != IFT_ETHER)
		return;

	ifp = malloc(sizeof(*ifp));
	if (ifp == NULL) {
		lwarn("%s index %u: allocation",
		    ifan->ifan_name, ifan->ifan_index);
		return;
	}

	ifp->if_key.if_index = ifan->ifan_index;
	strlcpy(ifp->if_key.if_name, ifan->ifan_name,
	    sizeof(ifp->if_key.if_name));

	ifp->if_lldpd = lldpd;
	TAILQ_INIT(&ifp->if_msaps);

	if (iface_insert(lldpd, ifp) != NULL) {
		lwarnx("%s index %u: already exists",
		    ifan->ifan_name, ifan->ifan_index);
		free(ifp);
		return;
	}

	linfo("%s index %u: attached", ifan->ifan_name, ifan->ifan_index);

	memset(&fmr, 0, sizeof(fmr));
	fmr.fmr_ifindex = ifp->if_key.if_index;
	memcpy(fmr.fmr_addr, maddr, ETHER_ADDR_LEN);

	if (setsockopt(EVENT_FD(&lldpd->en_ev),
	    IFT_ETHER, FRAME_ADD_MEMBERSHIP,
	    &fmr, sizeof(fmr)) == -1) {
		lwarn("%s index %u: add membership",
		    ifan->ifan_name, ifan->ifan_index);
	}
}

static void
rtsock_if_detach(struct lldpd *lldpd, const struct if_announcemsghdr *ifan)
{
	struct iface *ifp;
	struct lldp_msap *msap, *nmsap;
	struct frame_mreq fmr;

	ifp = iface_find(lldpd, ifan->ifan_name, ifan->ifan_index);
	if (ifp == NULL)
		return;

	memset(&fmr, 0, sizeof(fmr));
	fmr.fmr_ifindex = ifp->if_key.if_index;
	memcpy(fmr.fmr_addr, maddr, ETHER_ADDR_LEN);

	if (setsockopt(EVENT_FD(&lldpd->en_ev),
	    IFT_ETHER, FRAME_DEL_MEMBERSHIP,
	    &fmr, sizeof(fmr)) == -1) {
		lwarn("%s index %u: del membership",
		    ifp->if_key.if_name, ifp->if_key.if_index);
	}

	/* don't have to leave mcast group */
	linfo("%s index %u: detached", ifan->ifan_name, ifan->ifan_index);

	iface_remove(lldpd, ifp);

	TAILQ_FOREACH_SAFE(msap, &ifp->if_msaps, msap_entry, nmsap)
		lldp_msap_remove(ifp, msap);

	free(ifp);
}

static void
rtsock_ifannounce(struct lldpd *lldpd, const struct rt_msghdr *rtm, size_t len)
{
	const struct if_announcemsghdr *ifan;

	if (len < sizeof(*ifan)) {
		lwarnx("short ifannounce message: %zu < %zu", len,
		    sizeof(*ifan));
		return;
	}

	ifan = (const struct if_announcemsghdr *)rtm;
	if (ifan->ifan_index == 0) {
		lwarnx("%s index %u: %s() invalid index, ignoring",
		    ifan->ifan_name, ifan->ifan_index, __func__);
		return;
	}

	switch (ifan->ifan_what) {
	case IFAN_ARRIVAL:
		rtsock_if_attach(lldpd, ifan);
		break;
	case IFAN_DEPARTURE:
		rtsock_if_detach(lldpd, ifan);
		break;
	default:
		lwarnx("%s: %s index %u: unexpected ifannounce ifan %u",
		    __func__, ifan->ifan_name, ifan->ifan_index,
		    ifan->ifan_what);
		return;
	}
}

static void
rtsock_recv(int s, short events, void *arg)
{
	struct lldpd *lldpd = arg;
	char buf[1024];
	const struct rt_msghdr *rtm = (struct rt_msghdr *)buf;
	ssize_t rv;

	rv = recv(s, buf, sizeof(buf), 0);
	if (rv == -1) {
		lwarn("route message");
		return;
	}

	linfo("route message: %zd bytes", rv);
	if (rtm->rtm_version != RTM_VERSION) {
		lwarnx("routing message version %u not understood",
		    rtm->rtm_version);
		return;
	}

	switch (rtm->rtm_type) {
	case RTM_IFINFO:
		linfo("ifinfo");
		break;
	case RTM_IFANNOUNCE:
		rtsock_ifannounce(lldpd, rtm, rv);
		break;
	default:
		return;
	}
}

static void
ensock_open(struct lldpd *lldpd)
{
	struct sockaddr_frame sfrm = {
		.sfrm_family = AF_FRAME,
		.sfrm_proto = htons(ETHERTYPE_LLDP),
	};
	int opt;
	int s;

	s = socket(AF_FRAME, SOCK_DGRAM | SOCK_NONBLOCK, IFT_ETHER);
	if (s == -1)
		err(1, "AF_FRAME socket");

	opt = 1;
	if (setsockopt(s, IFT_ETHER, FRAME_RECVDSTADDR,
	    &opt, sizeof(opt)) == -1)
		err(1, "AF_FRAME setsockopt enable recv dstaddr");

	if (bind(s, (struct sockaddr *)&sfrm, sizeof(sfrm)) == -1)
		err(1, "AF_FRAME bind lldp");

	event_set(&lldpd->en_ev, s, 0, NULL, NULL);
}

static void
ensock_recv(int s, short events, void *arg)
{
	struct lldpd *lldpd = arg;
	struct sockaddr_frame sfrm;
	uint8_t buf[1500];

	static const struct ether_addr naddr;
	struct ether_addr *saddr = NULL;
	struct ether_addr *daddr = (struct ether_addr *)&naddr;
	struct cmsghdr *cmsg;
	union {
		struct cmsghdr hdr;
		uint8_t buf[CMSG_SPACE(sizeof(*daddr))];
	} cmsgbuf;
	struct iovec iov[1] = {
		{ .iov_base = buf, .iov_len = sizeof(buf) },
	};
	struct msghdr msg = {
		.msg_name = &sfrm,
		.msg_namelen = sizeof(sfrm),
		.msg_control = &cmsgbuf.buf,
		.msg_controllen = sizeof(cmsgbuf.buf),
		.msg_iov = iov,
		.msg_iovlen = 1,
	};
	ssize_t rv;
	size_t len;

	struct iface *ifp;
	struct iface_key key;
	struct lldp_msap *msap;

	struct tlv tlv;
	unsigned int tlvs;
	int ok;
	unsigned int idlen;
	unsigned int ttl;
	struct timeval age;
	int update = 0;

	rv = recvmsg(s, &msg, 0);
	if (rv == -1) {
		lwarn("Ethernet recv");
		return;
	}

	CMSG_FOREACH(cmsg, &msg) {
		if (CMSG_MATCH(cmsg,
		    sizeof(*daddr), IFT_ETHER, FRAME_RECVDSTADDR)) {
			daddr = (struct ether_addr *)CMSG_DATA(cmsg);
		}
	}
	saddr = (struct ether_addr *)sfrm.sfrm_addr;

	ldebug("%s: pdu from %s: %zd bytes", sfrm.sfrm_ifname,
	    ether_ntoa(saddr), rv);

	key.if_index = sfrm.sfrm_ifindex;
	ifp = RBT_FIND(ifaces, &lldpd->ifaces, (struct iface *)&key);
	if (ifp == NULL) {
		/* count */
		return;
	}

	/* XXX check if RX is enabled */
	agent_counter_inc(lldpd, ifp, statsFramesInTotal);

	len = rv;

	ok = tlv_first(&tlv, buf, len);
	if (ok != 1) {
		ldebug("%s: pdu from %s: first TLV extraction failed",
		    sfrm.sfrm_ifname, ether_ntoa(saddr));
		goto discard;
	}
	if (tlv.tlv_type != LLDP_TLV_CHASSIS_ID) {
		ldebug("%s: pdu from %s: first TLV type is not Chassis ID",
		    sfrm.sfrm_ifname, ether_ntoa(saddr));
		goto discard;
	}
	if (tlv.tlv_len < 2 || tlv.tlv_len > 256) {
		ldebug("%s: pdu from %s: "
		    "Chassis ID TLV length %u is out of range",
		    sfrm.sfrm_ifname, ether_ntoa(saddr),
		    tlv.tlv_len);
		goto discard;
	}

	ok = tlv_next(&tlv, buf, len);
	if (ok != 1) {
		ldebug("%s: pdu from %s: second TLV extraction failed",
		    sfrm.sfrm_ifname, ether_ntoa(saddr));
		goto discard;
	}
	if (tlv.tlv_type != LLDP_TLV_PORT_ID) {
		ldebug("%s: pdu from %s: first TLV type is not Port ID",
		    sfrm.sfrm_ifname, ether_ntoa(saddr));
		goto discard;
	}
	if (tlv.tlv_len < 2 || tlv.tlv_len > 256) {
		ldebug("%s: pdu from %s: "
		    "Port ID TLV length %u is out of range",
		    sfrm.sfrm_ifname, ether_ntoa(saddr),
		    tlv.tlv_len);
		goto discard;
	}

	ok = tlv_next(&tlv, buf, rv);
	if (ok != 1) {
		ldebug("%s: pdu from %s: third TLV extraction failed",
		    sfrm.sfrm_ifname, ether_ntoa(saddr));
		goto discard;
	}
	if (tlv.tlv_type != LLDP_TLV_TTL) {
		ldebug("%s: pdu from %s: third TLV type is not TTL",
		    sfrm.sfrm_ifname, ether_ntoa(saddr));
		goto discard;
	}
	if (tlv.tlv_len < 2) {
		ldebug("%s: pdu from %s: "
		    "TTL TLV length %u is too short",
		    sfrm.sfrm_ifname, ether_ntoa(saddr),
		    tlv.tlv_len);
		goto discard;
	}

	ttl = pdu_u16(tlv.tlv_payload);

        /*
	 * the tlv_offset points to the start of the current tlv,
	 * which is also the end of the previous tlv. the msap id is
	 * a concat of the first two tlvs, which is where the offset
	 * is now.
	 */
	idlen = tlv.tlv_offset;

	tlvs = (1 << LLDP_TLV_CHASSIS_ID) | (1 << LLDP_TLV_PORT_ID) |
	    (1 << LLDP_TLV_TTL);

	for (;;) {
		ok = tlv_next(&tlv, buf, rv);
		if (ok == -1) {
			lwarnx("TLV extraction failed");
			goto discard;
		}
		if (ok == 0)
			break;

		switch (tlv.tlv_type) {
		case LLDP_TLV_END:
			lwarnx("end of pdu with non-zero length");
			goto discard;

		case LLDP_TLV_CHASSIS_ID:
		case LLDP_TLV_PORT_ID:
		case LLDP_TLV_TTL:
		case LLDP_TLV_PORT_DESCR:
		case LLDP_TLV_SYSTEM_NAME:
		case LLDP_TLV_SYSTEM_DESCR:
		case LLDP_TLV_SYSTEM_CAP:
			if ((1 << tlv.tlv_type) & tlvs) {
				lwarnx("TLV type %u repeated", tlv.tlv_type);
				goto discard;
			}
			tlvs |= (1 << tlv.tlv_type);
			break;
		}
	}

	TAILQ_FOREACH(msap, &ifp->if_msaps, msap_entry) {
		if (msap->msap_id_len == idlen &&
		    memcmp(msap->msap_pdu, buf, idlen) == 0)
			break;
	}

	if (msap == NULL) {
		if (ttl == 0) {
			/* optimise DELETE_INFO below */
			return;
		}

		msap = malloc(sizeof(*msap));
		if (msap == NULL) {
			lwarn("%s: msap alloc", ifp->if_key.if_name);
			agent_counter_inc(lldpd, ifp,
			    statsFramesDiscardedTotal);
			return;
		}
		msap->msap_iface = ifp;
		msap->msap_pdu = NULL;
		msap->msap_pdu_len = 0;
		msap->msap_id_len = idlen;

		if (clock_gettime(CLOCK_BOOTTIME, &msap->msap_created) == -1)
			lerr(1, "CLOCK_BOOTTIME");

		msap->msap_updated.tv_sec = 0;
		msap->msap_updated.tv_nsec = 0;

		msap->msap_packets = 1;
		msap->msap_updates = 0;

		msap->msap_refs = 1;
		evtimer_set(&msap->msap_expiry, lldp_msap_expire, msap);
		TAILQ_INSERT_TAIL(&ifp->if_msaps, msap, msap_entry);
		TAILQ_INSERT_TAIL(&lldpd->msaps, msap, msap_aentry);
	}

	if (ttl == 0) {
		/* DELETE_INFO */
		if (msap->msap_pdu == NULL) {
			lwarnx("new msap DELETE_INFO");
			abort();
		}

		evtimer_del(&msap->msap_expiry);
		TAILQ_REMOVE(&ifp->if_msaps, msap, msap_entry);
		lldp_msap_rele(lldpd, msap);

		ldebug("%s: entry from %s deleted",
		    sfrm.sfrm_ifname, ether_ntoa(saddr));
		return;
	}

	if (len != msap->msap_pdu_len) {
		void *pdu = realloc(msap->msap_pdu, len);
		if (pdu == NULL) {
			lwarn("%s: pdu alloc", ifp->if_key.if_name);
			if (msap->msap_pdu == NULL) {
				TAILQ_REMOVE(&lldpd->msaps, msap,
				    msap_aentry);
				TAILQ_REMOVE(&ifp->if_msaps, msap,
				    msap_entry);
				free(msap);
			}
			agent_counter_inc(lldpd, ifp,
			    statsFramesDiscardedTotal);
			return;
		}
		msap->msap_pdu = pdu;
		msap->msap_pdu_len = len;
		update = 1;
	} else if (memcmp(msap->msap_pdu, buf, len) != 0)
		update = 1;

	if (update) {
		msap->msap_updates++;
		memcpy(msap->msap_pdu, buf, len);
		if (clock_gettime(CLOCK_BOOTTIME, &msap->msap_updated) == -1)
			lerr(1, "CLOCK_BOOTTIME");
	}
	msap->msap_saddr = *saddr;
	msap->msap_daddr = *daddr;
	msap->msap_packets++;

	age.tv_sec = ttl;
	age.tv_usec = 0;
	evtimer_add(&msap->msap_expiry, &age);

	return;

discard:
	agent_counter_inc(lldpd, ifp, statsFramesDiscardedTotal);
	agent_counter_inc(lldpd, ifp, statsFramesInErrorsTotal);
}

static void
ctlsock_open(struct lldpd *lldpd)
{
	struct sockaddr_un sun = {
		.sun_family = AF_UNIX,
	};
	const char *path = lldpd->ctl_path;
	mode_t oumask;
	int s;

	if (strlcpy(sun.sun_path, path, sizeof(sun.sun_path)) >=
	    sizeof(sun.sun_path))
		errc(ENAMETOOLONG, 1, "control socket %s", path);

	s = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0);
	if (s == -1)
		err(1, "control socket");

	/* try connect first? */

	if (unlink(path) == -1) {
		if (errno != ENOENT)
			err(1, "control socket %s unlink", path);
	}

	oumask = umask(S_IXUSR|S_IXGRP|S_IWOTH|S_IROTH|S_IXOTH);
	if (oumask == -1)
		err(1, "umask");
	if (bind(s, (struct sockaddr *)&sun, sizeof(sun)) == -1)
		err(1, "control socket %s bind", path);
	if (umask(oumask) == -1)
		err(1, "umask restore");

	if (chmod(path, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP) == -1)
		err(1, "control socket %s chmod", path);

	if (listen(s, 5) == -1)
		err(1, "control socket %s listen", path);

	event_set(&lldpd->ctl_ev, s, 0, NULL, NULL);
}

static void
ctl_close(struct lldpd *lldpd, struct lldpd_ctl *ctl)
{
	int fd = EVENT_FD(&ctl->ctl_rd_ev);

	event_del(&ctl->ctl_rd_ev);
	event_del(&ctl->ctl_wr_ev);
	free(ctl);
	close(fd);
}

static ssize_t		ctl_ping(struct lldpd *, struct lldpd_ctl *,
			    const void *, size_t);
static ssize_t		ctl_msap_req(struct lldpd *, struct lldpd_ctl *,
			    const void *, size_t);

static void
ctl_recv(int fd, short events, void *arg)
{
	struct lldpd_ctl *ctl = arg;
	struct lldpd *lldpd = ctl->ctl_lldpd;
	unsigned int msgtype;
	uint8_t buf[4096];
	struct iovec iov[2] = {
	    { &msgtype, sizeof(msgtype) },
	    { buf, sizeof(buf) },
	};
	struct msghdr msg = {
		.msg_iov = iov,
		.msg_iovlen = nitems(iov),
	};
	ssize_t rv;
	size_t len;

	rv = recvmsg(fd, &msg, 0);
	if (rv == -1) {
		lwarn("ctl recv");
		return;
	}
	if (rv == 0) {
		ctl_close(lldpd, ctl);
		return;
	}

	if (ctl->ctl_handler != NULL) {
		ldebug("ctl recv while not idle");
		ctl_close(lldpd, ctl);
		return;
	}

	len = rv;
	ldebug("%s: msgtype %u, %zu bytes", __func__, msgtype, len);
	if (len < sizeof(msgtype)) {
		/* short message */
		ctl_close(lldpd, ctl);
		return;
	}
	len -= sizeof(msgtype);

	switch (msgtype) {
	case LLDP_CTL_MSG_PING:
		rv = ctl_ping(lldpd, ctl, buf, len);
		break;
	case LLDP_CTL_MSG_MSAP_REQ:
		rv = ctl_msap_req(lldpd, ctl, buf, len);
		break;
	default:
		lwarnx("%s: unhandled message %u", __func__, msgtype);
		rv = -1;
		return;
	}

	if (rv == -1) {
		ctl_close(lldpd, ctl);
		return;
	}

	(*ctl->ctl_handler)(lldpd, ctl, fd);
}

static void
ctl_done(struct lldpd *lldpd, struct lldpd_ctl *ctl)
{
	ctl->ctl_handler = NULL;
	ctl->ctl_ctx = NULL;
}

static void
ctl_pong(struct lldpd *lldpd, struct lldpd_ctl *ctl, int fd)
{
	unsigned int msgtype = LLDP_CTL_MSG_PONG;
	struct iovec *piov = ctl->ctl_ctx;
	struct iovec iov[2] = {
	    { &msgtype, sizeof(msgtype) },
	    *piov,
	};
	struct msghdr msg = {
		.msg_iov = iov,
		.msg_iovlen = nitems(iov),
	};
	ssize_t rv;

	rv = sendmsg(fd, &msg, 0);
	if (rv == -1) {
		switch (errno) {
		case EINTR:
		case EAGAIN:
			event_add(&ctl->ctl_wr_ev, NULL);
			return;
		default:
			lwarn("%s", __func__);
			break;
		}

		ctl_close(lldpd, ctl);
		return;
	}

	free(piov->iov_base);
	free(piov);

	ctl_done(lldpd, ctl);
}

static ssize_t
ctl_ping(struct lldpd *lldpd, struct lldpd_ctl *ctl,
    const void *buf, size_t len)
{
	struct iovec *iov;

	iov = malloc(sizeof(*iov));
	if (iov == NULL) {
		lwarn("%s iovec", __func__);
		return (-1);
	}

	iov->iov_base = malloc(len);
	if (iov->iov_base == NULL) {
		lwarn("%s", __func__);
		free(iov);
		return (-1);
	}
	memcpy(iov->iov_base, buf, len);
	iov->iov_len = len;

	ctl->ctl_handler = ctl_pong;
	ctl->ctl_ctx = iov;

	return (0);
}

static void
ctl_send(int fd, short events, void *arg)
{
	struct lldpd_ctl *ctl = arg;
	struct lldpd *lldpd = ctl->ctl_lldpd;

	ctl->ctl_handler(lldpd, ctl, fd);
}

struct ctl_msap_ctx {
	char ifname[IFNAMSIZ];
	struct lldp_msap *msap;
};

static void		ctl_msap(struct lldpd *, struct lldpd_ctl *, int);
static ssize_t		ctl_msap_req_next(struct lldpd *, struct lldpd_ctl *,
			    struct lldp_msap *);
static void		ctl_msap_req_end(struct lldpd *, struct lldpd_ctl *,
			    int);

static ssize_t
ctl_msap_req(struct lldpd *lldpd, struct lldpd_ctl *ctl,
    const void *buf, size_t len)
{
	const struct lldp_ctl_msg_msap_req *req;
	struct ctl_msap_ctx *ctx;

	if (len != sizeof(*req)) {
		lwarnx("%s req len", __func__);
		return (-1);
	}
	req = buf;

	ctx = malloc(sizeof(*ctx));
	if (ctx == NULL) {
		lwarnx("%s ctx", __func__);
		return (-1);
	}
	memcpy(ctx->ifname, req->ifname, sizeof(ctx->ifname));

	ctl->ctl_handler = ctl_msap;
	ctl->ctl_ctx = ctx;

	return (ctl_msap_req_next(lldpd, ctl, TAILQ_FIRST(&lldpd->msaps)));
}

static void
ctl_msap(struct lldpd *lldpd, struct lldpd_ctl *ctl, int fd)
{
	struct ctl_msap_ctx *ctx = ctl->ctl_ctx;
	struct lldp_msap *msap = ctx->msap;
	struct iface *ifp;
	ssize_t rv;

	unsigned int msgtype = LLDP_CTL_MSG_MSAP;
	struct lldp_ctl_msg_msap msg_msap;
	struct iovec iov[3] = {
	    { &msgtype, sizeof(msgtype) },
	    { &msg_msap, sizeof(msg_msap) }
	};
	struct msghdr msg = {
		.msg_iov = iov,
		.msg_iovlen = nitems(iov),
	};

	memset(&msg_msap, 0, sizeof(msg_msap));

	ifp = msap->msap_iface;
	if (ifp != NULL) {
		strlcpy(msg_msap.ifname, ifp->if_key.if_name,
		    sizeof(msg_msap.ifname));
	}
	msg_msap.saddr = msap->msap_saddr;
	msg_msap.daddr = msap->msap_daddr;
	msg_msap.created = msap->msap_created;
	msg_msap.updated = msap->msap_updated;
	msg_msap.packets = msap->msap_packets;
	msg_msap.updates = msap->msap_updates;

	iov[2].iov_base = msap->msap_pdu;
	iov[2].iov_len = msap->msap_pdu_len;

	rv = sendmsg(fd, &msg, 0);
	if (rv == -1) {
		switch (errno) {
		case EINTR:
		case EAGAIN:
			event_add(&ctl->ctl_wr_ev, NULL);
			return;
		default:
			lwarn("ctl send");
			break;
		}

		lldp_msap_rele(lldpd, msap);
		free(ctx);
		ctl_close(lldpd, ctl);
		return;
	}

	rv = ctl_msap_req_next(lldpd, ctl, TAILQ_NEXT(msap, msap_aentry));
        lldp_msap_rele(lldpd, msap);

	if (rv == -1) {
		free(ctx);
		ctl_close(lldpd, ctl);
		return;
	}

	event_add(&ctl->ctl_wr_ev, NULL);
}

static ssize_t
ctl_msap_req_next(struct lldpd *lldpd, struct lldpd_ctl *ctl,
    struct lldp_msap *msap)
{
	struct ctl_msap_ctx *ctx = ctl->ctl_ctx;
	struct iface *ifp;

	for (;;) {
		if (msap == NULL) {
			ctl->ctl_handler = ctl_msap_req_end;
			return (0);
		}

		if (ctx->ifname[0] == '\0')
			break;
		ifp = msap->msap_iface;
		if (ifp != NULL && strncmp(ifp->if_key.if_name, ctx->ifname,
		    sizeof(ifp->if_key.if_name)) == 0)
			break;

		msap = TAILQ_NEXT(msap, msap_aentry);
	}

	ctx->msap = lldp_msap_take(lldpd, msap);
	return (0);
}

static void
ctl_msap_req_end(struct lldpd *lldpd, struct lldpd_ctl *ctl, int fd)
{
	struct ctl_msap_ctx *ctx = ctl->ctl_ctx;

	unsigned int msgtype = LLDP_CTL_MSG_MSAP_END;
	ssize_t rv;

	rv = send(fd, &msgtype, sizeof(msgtype), 0);
	if (rv == -1) {
		switch (errno) {
		case EINTR:
		case EAGAIN:
			event_add(&ctl->ctl_wr_ev, NULL);
			return;
		default:
			lwarn("%s", __func__);
			break;
		}

		free(ctx);
		ctl_close(lldpd, ctl);
		return;
	}

	free(ctx);
	ctl_done(lldpd, ctl);
}

static void
ctlsock_accept(int s, short events, void *arg)
{
	struct lldpd *lldpd = arg;
	struct lldpd_ctl *ctl;
	int fd;

	fd = accept4(s, NULL, NULL, SOCK_NONBLOCK);
	if (fd == -1) {
		lwarn("control socket %s accept", lldpd->ctl_path);
		return;
	}

	ctl = malloc(sizeof(*ctl));
	if (ctl == NULL) {
		lwarn("ctl alloc");
		close(fd);
		return;
	}
	ctl->ctl_lldpd = lldpd;
	ctl->ctl_handler = NULL;
	ctl->ctl_ctx = NULL;

	if (getpeereid(fd, &ctl->ctl_peer_uid, &ctl->ctl_peer_gid) == -1)
		err(1, "ctl getpeereid");

	event_set(&ctl->ctl_rd_ev, fd, EV_READ|EV_PERSIST,
	    ctl_recv, ctl);
	event_set(&ctl->ctl_wr_ev, fd, EV_WRITE,
	    ctl_send, ctl);

	event_add(&ctl->ctl_rd_ev, NULL);
}

static int
getall(struct lldpd *lldpd)
{
	struct ifaddrs *ifa0, *ifa;
	struct sockaddr_dl *sdl;
	struct if_data *ifi;
	struct iface *ifp;
	struct frame_mreq fmr;

	if (getifaddrs(&ifa0) == -1)
		return (-1);

	for (ifa = ifa0; ifa != NULL; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr->sa_family != AF_LINK)
			continue;
		ifi = ifa->ifa_data;
		if (ifi->ifi_type != IFT_ETHER)
			continue;

		sdl = (struct sockaddr_dl *)ifa->ifa_addr;
		if (sdl->sdl_index == 0) {
			warnx("interface %s has index 0, skipping",
			    ifa->ifa_name);
			continue;
		}

		ldebug("%s index %u", ifa->ifa_name, sdl->sdl_index);

		ifp = malloc(sizeof(*ifp));
		if (ifp == NULL) {
			warn("interface %s allocation", ifa->ifa_name);
			continue;
		}

		ifp->if_key.if_index = sdl->sdl_index;
		strlcpy(ifp->if_key.if_name, ifa->ifa_name,
		    sizeof(ifp->if_key.if_name));

		ifp->if_lldpd = lldpd;
		TAILQ_INIT(&ifp->if_msaps);

		if (RBT_INSERT(ifaces, &lldpd->ifaces, ifp) != NULL) {
			warnx("interface %s: index %u already exists",
			    ifa->ifa_name, ifp->if_key.if_index);
			free(ifp);
			continue;
		}

		memset(&fmr, 0, sizeof(fmr));
		fmr.fmr_ifindex = ifp->if_key.if_index;
		memcpy(fmr.fmr_addr, maddr, ETHER_ADDR_LEN);

		if (setsockopt(EVENT_FD(&lldpd->en_ev),
		    IFT_ETHER, FRAME_ADD_MEMBERSHIP,
		    &fmr, sizeof(fmr)) == -1)
			warn("interface %s: add membership", ifa->ifa_name);
	}

	freeifaddrs(ifa0);

	return (0);
}

RBT_GENERATE(ifaces, iface, if_entry, iface_cmp);

/* daemon(3) clone, intended to be used in a "r"estricted environment */
int
rdaemon(int devnull)
{
	if (devnull == -1) {
		errno = EBADF;
		return (-1);
	} 
	if (fcntl(devnull, F_GETFL) == -1)
		return (-1);

	switch (fork()) {
	case -1:
		return (-1);
	case 0:
		break;
	default:
		_exit(0);
	}

	if (setsid() == -1)
		return (-1);

	(void)dup2(devnull, STDIN_FILENO);
	(void)dup2(devnull, STDOUT_FILENO);
	(void)dup2(devnull, STDERR_FILENO);
	if (devnull > 2)
		(void)close(devnull);

	return (0);
}
