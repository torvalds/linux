/*	$OpenBSD: spamlogd.c,v 1.32 2021/07/12 15:09:18 beck Exp $	*/

/*
 * Copyright (c) 2006 Henning Brauer <henning@openbsd.org>
 * Copyright (c) 2006 Berk D. Demir.
 * Copyright (c) 2004-2007 Bob Beck.
 * Copyright (c) 2001 Theo de Raadt.
 * Copyright (c) 2001 Can Erkin Acar.
 * All rights reserved
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

/* watch pf log for mail connections, update whitelist entries. */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/signal.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>

#include <net/pfvar.h>
#include <net/if_pflog.h>

#include <db.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pwd.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>
#include <unistd.h>
#include <pcap-int.h>
#include <pcap.h>

#include "grey.h"
#include "sync.h"

#define MIN_PFLOG_HDRLEN	45
#define PCAPSNAP		512
#define PCAPTIMO		500	/* ms */
#define PCAPOPTZ		1	/* optimize filter */
#define PCAPFSIZ		512	/* pcap filter string size */

#define SPAMD_USER		"_spamd"

int debug = 1;
int greylist = 1;
FILE *grey = NULL;

u_short sync_port;
int syncsend;
u_int8_t		 flag_debug = 0;
u_int8_t		 flag_inbound = 0;
char			*networkif = NULL;
char			*pflogif = "pflog0";
char			 errbuf[PCAP_ERRBUF_SIZE];
pcap_t			*hpcap = NULL;
struct syslog_data	 sdata	= SYSLOG_DATA_INIT;
time_t			 whiteexp = WHITEEXP;
extern char		*__progname;

void	logmsg(int , const char *, ...);
void	sighandler_close(int);
int	init_pcap(void);
void	logpkt_handler(u_char *, const struct pcap_pkthdr *, const u_char *);
int	dbupdate(char *, char *);
__dead void	usage(void);

void
logmsg(int pri, const char *msg, ...)
{
	va_list	ap;
	va_start(ap, msg);

	if (flag_debug) {
		vfprintf(stderr, msg, ap);
		fprintf(stderr, "\n");
	} else
		vsyslog_r(pri, &sdata, msg, ap);

	va_end(ap);
}

void
sighandler_close(int signal)
{
	if (hpcap != NULL)
		pcap_breakloop(hpcap);	/* sighdlr safe */
}

pcap_t *
pflog_read_live(const char *source, int slen, int promisc, int to_ms,
    char *ebuf)
{
	int		fd;
	struct bpf_version bv;
	struct ifreq	ifr;
	u_int		v, dlt = DLT_PFLOG;
	pcap_t		*p;

	if (source == NULL || slen <= 0)
		return (NULL);

	p = pcap_create(source, ebuf);
	if (p == NULL)
		return (NULL);

	/* Open bpf(4) read only */
	if ((fd = open("/dev/bpf", O_RDONLY)) == -1)
		return (NULL);

	if (ioctl(fd, BIOCVERSION, &bv) == -1) {
		snprintf(ebuf, PCAP_ERRBUF_SIZE, "BIOCVERSION: %s",
		    pcap_strerror(errno));
		goto bad;
	}

	if (bv.bv_major != BPF_MAJOR_VERSION ||
	    bv.bv_minor < BPF_MINOR_VERSION) {
		snprintf(ebuf, PCAP_ERRBUF_SIZE,
		    "kernel bpf filter out of date");
		goto bad;
	}

	strlcpy(ifr.ifr_name, source, sizeof(ifr.ifr_name));
	if (ioctl(fd, BIOCSETIF, &ifr) == -1) {
		snprintf(ebuf, PCAP_ERRBUF_SIZE, "BIOCSETIF: %s",
		    pcap_strerror(errno));
		goto bad;
	}

	if (dlt != (u_int) -1 && ioctl(fd, BIOCSDLT, &dlt)) {
		snprintf(ebuf, PCAP_ERRBUF_SIZE, "BIOCSDLT: %s",
		    pcap_strerror(errno));
		goto bad;
	}

	p->fd = fd;
	p->snapshot = slen;
	p->linktype = dlt;

	/* set timeout */
	if (to_ms != 0) {
		struct timeval to;
		to.tv_sec = to_ms / 1000;
		to.tv_usec = (to_ms * 1000) % 1000000;
		if (ioctl(p->fd, BIOCSRTIMEOUT, &to) == -1) {
			snprintf(ebuf, PCAP_ERRBUF_SIZE, "BIOCSRTIMEOUT: %s",
			    pcap_strerror(errno));
			goto bad;
		}
	}
	if (promisc)
		/* this is allowed to fail */
		ioctl(fd, BIOCPROMISC, NULL);

	if (ioctl(fd, BIOCGBLEN, &v) == -1) {
		snprintf(ebuf, PCAP_ERRBUF_SIZE, "BIOCGBLEN: %s",
		    pcap_strerror(errno));
		goto bad;
	}
	p->bufsize = v;
	p->buffer = malloc(p->bufsize);
	if (p->buffer == NULL) {
		snprintf(ebuf, PCAP_ERRBUF_SIZE, "malloc: %s",
		    pcap_strerror(errno));
		goto bad;
	}
	p->activated = 1;
	return (p);

bad:
	pcap_close(p);
	return (NULL);
}

int
init_pcap(void)
{
	struct bpf_program	bpfp;
	char	filter[PCAPFSIZ] = "ip and port 25 and action pass "
		    "and tcp[13]&0x12=0x2";

	if ((hpcap = pflog_read_live(pflogif, PCAPSNAP, 1, PCAPTIMO,
	    errbuf)) == NULL) {
		logmsg(LOG_ERR, "Failed to initialize: %s", errbuf);
		return (-1);
	}

	if (pcap_datalink(hpcap) != DLT_PFLOG) {
		logmsg(LOG_ERR, "Invalid datalink type");
		pcap_close(hpcap);
		hpcap = NULL;
		return (-1);
	}

	if (networkif != NULL) {
		strlcat(filter, " and on ", PCAPFSIZ);
		strlcat(filter, networkif, PCAPFSIZ);
	}

	if (pcap_compile(hpcap, &bpfp, filter, PCAPOPTZ, 0) == -1 ||
	    pcap_setfilter(hpcap, &bpfp) == -1) {
		logmsg(LOG_ERR, "%s", pcap_geterr(hpcap));
		return (-1);
	}

	pcap_freecode(&bpfp);

	if (ioctl(pcap_fileno(hpcap), BIOCLOCK) == -1) {
		logmsg(LOG_ERR, "BIOCLOCK: %s", strerror(errno));
		return (-1);
	}

	return (0);
}

void
logpkt_handler(u_char *user, const struct pcap_pkthdr *h, const u_char *sp)
{
	sa_family_t		 af;
	u_int8_t		 hdrlen;
	u_int32_t		 caplen = h->caplen;
	const struct ip		*ip = NULL;
	const struct pfloghdr	*hdr;
	char			 ipstraddr[40] = { '\0' };

	hdr = (const struct pfloghdr *)sp;
	if (hdr->length < MIN_PFLOG_HDRLEN) {
		logmsg(LOG_WARNING, "invalid pflog header length (%u/%u). "
		    "packet dropped.", hdr->length, MIN_PFLOG_HDRLEN);
		return;
	}
	hdrlen = BPF_WORDALIGN(hdr->length);

	if (caplen < hdrlen) {
		logmsg(LOG_WARNING, "pflog header larger than caplen (%u/%u). "
		    "packet dropped.", hdrlen, caplen);
		return;
	}

	/* We're interested in passed packets */
	if (hdr->action != PF_PASS)
		return;

	af = hdr->af;
	if (af == AF_INET) {
		ip = (const struct ip *)(sp + hdrlen);
		if (hdr->dir == PF_IN)
			inet_ntop(af, &ip->ip_src, ipstraddr,
			    sizeof(ipstraddr));
		else if (hdr->dir == PF_OUT && !flag_inbound)
			inet_ntop(af, &ip->ip_dst, ipstraddr,
			    sizeof(ipstraddr));
	}

	if (ipstraddr[0] != '\0') {
		if (hdr->dir == PF_IN)
			logmsg(LOG_DEBUG,"inbound %s", ipstraddr);
		else 
			logmsg(LOG_DEBUG,"outbound %s", ipstraddr);
		dbupdate(PATH_SPAMD_DB, ipstraddr);
	}
}

int
dbupdate(char *dbname, char *ip)
{
	HASHINFO	hashinfo;
	DBT		dbk, dbd;
	DB		*db;
	struct gdata	gd;
	time_t		now;
	int		r;
	struct in_addr	ia;

	now = time(NULL);
	memset(&hashinfo, 0, sizeof(hashinfo));
	db = dbopen(dbname, O_EXLOCK|O_RDWR, 0600, DB_HASH, &hashinfo);
	if (db == NULL) {
		logmsg(LOG_ERR, "Can not open db %s: %s", dbname,
		    strerror(errno));
		return (-1);
	}
	if (inet_pton(AF_INET, ip, &ia) != 1) {
		logmsg(LOG_NOTICE, "Invalid IP address %s", ip);
		goto bad;
	}
	memset(&dbk, 0, sizeof(dbk));
	dbk.size = strlen(ip);
	dbk.data = ip;
	memset(&dbd, 0, sizeof(dbd));

	/* add or update whitelist entry */
	r = db->get(db, &dbk, &dbd, 0);
	if (r == -1) {
		logmsg(LOG_NOTICE, "db->get failed (%m)");
		goto bad;
	}

	if (r) {
		/* new entry */
		memset(&gd, 0, sizeof(gd));
		gd.first = now;
		gd.bcount = 1;
		gd.pass = now;
		gd.expire = now + whiteexp;
		memset(&dbk, 0, sizeof(dbk));
		dbk.size = strlen(ip);
		dbk.data = ip;
		memset(&dbd, 0, sizeof(dbd));
		dbd.size = sizeof(gd);
		dbd.data = &gd;
		r = db->put(db, &dbk, &dbd, 0);
		if (r) {
			logmsg(LOG_NOTICE, "db->put failed (%m)");
			goto bad;
		}
	} else {
		/* XXX - backwards compat */
		if (gdcopyin(&dbd, &gd) == -1) {
			/* whatever this is, it doesn't belong */
			db->del(db, &dbk, 0);
			goto bad;
		}
		gd.pcount++;
		gd.expire = now + whiteexp;
		memset(&dbk, 0, sizeof(dbk));
		dbk.size = strlen(ip);
		dbk.data = ip;
		memset(&dbd, 0, sizeof(dbd));
		dbd.size = sizeof(gd);
		dbd.data = &gd;
		r = db->put(db, &dbk, &dbd, 0);
		if (r) {
			logmsg(LOG_NOTICE, "db->put failed (%m)");
			goto bad;
		}
	}
	db->close(db);
	db = NULL;
	if (syncsend)
		sync_white(now, now + whiteexp, ip);
	return (0);
 bad:
	db->close(db);
	db = NULL;
	return (-1);
}

void
usage(void)
{
	fprintf(stderr,
	    "usage: %s [-DI] [-i interface] [-l pflog_interface] "
	    "[-W whiteexp] [-Y synctarget]\n",
	    __progname);
	exit(1);
}

int
main(int argc, char **argv)
{
	int		 ch;
	struct passwd	*pw;
	pcap_handler	 phandler = logpkt_handler;
	int syncfd = 0;
	struct servent *ent;
	char *sync_iface = NULL;
	char *sync_baddr = NULL;
	const char *errstr;

	if (geteuid())
		errx(1, "need root privileges");

	if ((ent = getservbyname("spamd-sync", "udp")) == NULL)
		errx(1, "Can't find service \"spamd-sync\" in /etc/services");
	sync_port = ntohs(ent->s_port);

	while ((ch = getopt(argc, argv, "DIi:l:W:Y:")) != -1) {
		switch (ch) {
		case 'D':
			flag_debug = 1;
			break;
		case 'I':
			flag_inbound = 1;
			break;
		case 'i':
			networkif = optarg;
			break;
		case 'l':
			pflogif = optarg;
			break;
		case 'W':
			/* limit whiteexp to 2160 hours (90 days) */
			whiteexp = strtonum(optarg, 1, (24 * 90), &errstr);
			if (errstr)
				usage();
			/* convert to seconds from hours */
			whiteexp *= (60 * 60);
			break;
		case 'Y':
			if (sync_addhost(optarg, sync_port) != 0)
				sync_iface = optarg;
			syncsend++;
			break;
		default:
			usage();
		}
	}

	signal(SIGINT , sighandler_close);
	signal(SIGQUIT, sighandler_close);
	signal(SIGTERM, sighandler_close);

	logmsg(LOG_DEBUG, "Listening on %s for %s %s", pflogif,
	    (networkif == NULL) ? "all interfaces." : networkif,
	    (flag_inbound) ? "Inbound direction only." : "");

	if (init_pcap() == -1)
		err(1, "couldn't initialize pcap");

	if (syncsend) {
		syncfd = sync_init(sync_iface, sync_baddr, sync_port);
		if (syncfd == -1)
			err(1, "sync init");
	}

	/* privdrop */
	if ((pw = getpwnam(SPAMD_USER)) == NULL)
		errx(1, "no such user %s", SPAMD_USER);

	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		err(1, "failed to drop privs");

	if (!flag_debug) {
		if (daemon(0, 0) == -1)
			err(1, "daemon");
		tzset();
		openlog_r("spamlogd", LOG_PID | LOG_NDELAY, LOG_DAEMON, &sdata);
	}

	if (unveil(PATH_SPAMD_DB, "rw") == -1)
		err(1, "unveil %s", PATH_SPAMD_DB);
	if (syncsend) {
		if (pledge("stdio rpath wpath inet flock", NULL) == -1)
			err(1, "pledge");
	} else {
		if (pledge("stdio rpath wpath flock", NULL) == -1)
			err(1, "pledge");
	}

	pcap_loop(hpcap, -1, phandler, NULL);

	logmsg(LOG_NOTICE, "exiting");
	if (!flag_debug)
		closelog_r(&sdata);

	exit(0);
}
