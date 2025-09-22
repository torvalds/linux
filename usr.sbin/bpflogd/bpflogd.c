/*	$OpenBSD: bpflogd.c,v 1.7 2025/05/21 04:50:38 kn Exp $	*/

/*
 * Copyright (c) 2025 The University of Queensland
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

/*
 * This code was written by David Gwynne <dlg@uq.edu.au> as part
 * of the Information Technology Infrastructure Group (ITIG) in the
 * Faculty of Engineering, Architecture and Information Technology
 * (EAIT).
 */

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <pwd.h>
#include <errno.h>
#include <err.h>

#include <sys/queue.h>

#include <net/if.h>
#include <net/bpf.h>

#include <event.h>
#include <pcap.h>

#include "log.h"

#ifndef nitems
#define nitems(_a) (sizeof((_a)) / sizeof((_a)[0]))
#endif

#define BPFLOGD_USER "_pflogd"

#define _DEV_BPF "/dev/bpf"

struct bpflogd;

struct bpfif {
	struct bpflogd		*bif_bd;
	const char		*bif_name;
	int			 bif_bpf;

	struct event		 bif_ev;

	TAILQ_ENTRY(bpfif)	 bif_entry;
};

TAILQ_HEAD(bpfif_list, bpfif);

struct bpflogd {
	const char		*bd_user;
	const char		*bd_fname;
	int			 bd_fd;
	unsigned int		 bd_snaplen;
	int			 bd_dlt;
	const char		*bd_dlt_name;

	struct bpfif_list	 bd_bif_list;

	int			 bd_buflen;
	void			*bd_buf;

	struct event		 bd_sighup;
};

static int		bpf_maxbufsize(void);

static void		bpflogd_hup(int, short, void *);

static void		bpfif_open(struct bpfif *);
static void		bpfif_read(int, short, void *);

static int		bpflog_open(struct bpflogd *);

__dead static void
usage(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s [-dPp] [-F filterfile] [-s snaplen] "
	    "[-u user] [-w waitms]\n"
	    "\t" "[-y datalinktype] -f filename -i interface [expression ...]\n",
	    __progname);

	exit(1);
}

int
main(int argc, char *argv[])
{
	struct bpflogd _bd = {
		.bd_user = BPFLOGD_USER,
		.bd_fname = NULL,
		.bd_fd = -1,
		.bd_buflen = bpf_maxbufsize(),
		.bd_snaplen = BPF_MAXBUFSIZE,
		.bd_dlt = -1,
		.bd_bif_list = TAILQ_HEAD_INITIALIZER(_bd.bd_bif_list),
	};
	struct bpflogd *bd = &_bd;
	struct bpfif *bif, *bif0;
	struct bpf_version bv;
	char *filter = NULL;
	struct bpf_program bf;
	struct bpf_insn insns[] = { { BPF_RET, 0, 0, -1 } };
	int debug = 0;
	int promisc = 0;
	int waitms = 1000;
	struct timeval waittv;
	struct passwd *pw;

	int ch;
	const char *errstr;

	while ((ch = getopt(argc, argv, "df:F:i:pPs:u:w:y:")) != -1) {
		switch (ch) {
		case 'd':
			debug = 1;
			break;
		case 'f':
			bd->bd_fname = optarg;
			break;
		case 'F':
			filter = optarg;
			break;
		case 'i':
			TAILQ_FOREACH(bif, &bd->bd_bif_list, bif_entry) {
				if (strcmp(bif->bif_name, optarg) == 0) {
					errx(1, "interface %s already exists",
					    optarg);
				}
			}

			bif = malloc(sizeof(*bif));
			if (bif == NULL)
				err(1, "bpf interface alloc");
			bif->bif_bd = bd;
			bif->bif_name = optarg;
			TAILQ_INSERT_TAIL(&bd->bd_bif_list, bif, bif_entry);
			break;
		case 'p':
			promisc = 0;
			break;
		case 'P':
			promisc = 1;
			break;
		case 's':
			bd->bd_snaplen = strtonum(optarg, 60, BPF_MAXBUFSIZE,
			    &errstr);
			if (errstr != NULL)
				errx(1, "snaplen: %s", errstr);
			break;
		case 'u':
			bd->bd_user = optarg;
			break;
		case 'w':
			waitms = strtonum(optarg, 10, 300000, &errstr);
			if (errstr != NULL)
				errx(1, "wait ms: %s", errstr);
			break;
		case 'y':
			bd->bd_dlt = pcap_datalink_name_to_val(optarg);
			if (bd->bd_dlt == -1) {
				errx(1, "%s: unknown datalink type name",
				    optarg);
			}
			bd->bd_dlt_name = optarg;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}

	argc -= optind;
	argv += optind;

	if (bd->bd_fname == NULL) {
		warnx("output file not specified");
		usage();
	}

	bif0 = TAILQ_FIRST(&bd->bd_bif_list);
	if (bif0 == NULL) {
		warnx("no interfaces specified");
		usage();
	}

	if (filter != NULL && argc > 0) {
		warnx("use either -F or extra arguments, not both");
		usage();
	}

	signal(SIGPIPE, SIG_IGN);

	if (geteuid())
		errx(1, "need root privileges");

	pw = getpwnam(bd->bd_user);
	if (pw == NULL)
		errx(1, "%s: unknown user", bd->bd_user);

	bd->bd_buf = malloc(bd->bd_buflen);
	if (bd->bd_buf == NULL)
		err(1, "bpf read buffer");

	bpfif_open(bif0); /* err on failure */

	if (ioctl(bif0->bif_bpf, BIOCVERSION, &bv) == -1)
		err(1, "%s: get filter language version", bif0->bif_name);

	if (bv.bv_major != BPF_MAJOR_VERSION) {
		errx(1, "bpf major %u, expected %u",
		    bv.bv_major, BPF_MAJOR_VERSION);
	}
	if (bv.bv_minor < BPF_MINOR_VERSION) {
		errx(1, "bpf minor %u, expected >= %u",
		    bv.bv_minor, BPF_MINOR_VERSION);
	}

	if (bd->bd_dlt != -1) {
		if (ioctl(bif0->bif_bpf, BIOCSDLT, &bd->bd_dlt) == -1) {
			err(1, "%s: unsupported datalink type %s",
			    bif0->bif_name, bd->bd_dlt_name);
		}
	} else {
		if (ioctl(bif0->bif_bpf, BIOCGDLT, &bd->bd_dlt) == -1)
			err(1, "%s: get datalink type", bif0->bif_name);

		bd->bd_dlt_name = pcap_datalink_val_to_name(bd->bd_dlt);
		if (bd->bd_dlt_name == NULL) {
			errx(1, "%s: datalink type %d is unknown to libpcap",
			    bif0->bif_name, bd->bd_dlt);
		}
	}

	if (filter != NULL || argc > 0) {
		pcap_t *ph;
		char *expr = NULL;
		int i;

		if (filter != NULL) {
			int fd;
			ssize_t rv;

			fd = open(filter, O_RDONLY);
			if (fd == -1)
				err(1, "%s", filter);

#define BPFLOG_FILTER_MAX	8192

			expr = malloc(BPFLOG_FILTER_MAX);
			if (expr == NULL)
				err(1, NULL);

			rv = read(fd, expr, BPFLOG_FILTER_MAX);
			if (rv == -1)
				err(1, "%s read", filter);
			if (rv == 0)
				errx(1, "%s is empty", filter);
			if (rv >= BPFLOG_FILTER_MAX - 1)
				errx(1, "%s is too long", filter);

			close(fd);
		} else if (argc == 1)
			expr = argv[0];
		else {
			size_t alen = strlen(argv[0]);
			size_t len = alen;

			expr = malloc(len + 1);
			if (expr == NULL)
				err(1, "bpf expression buffer");

			memcpy(expr, argv[0], alen);

			for (i = 1; i < argc; i++) {
				size_t nlen;

				alen = strlen(argv[i]);
				if (alen == 0)
					continue;

				nlen = len + 1 + alen;

				expr = realloc(expr, nlen + 1);
				if (expr == NULL)
					err(1, "bpf expression buffer");

				expr[len] = ' ';
				memcpy(expr + len + 1, argv[i], alen);

				len = nlen;
			}
			expr[len] = '\0';
		}

		ph = pcap_open_dead(bd->bd_dlt, bd->bd_snaplen);
		if (ph == NULL)
			err(1, "pcap_open_dead");

		if (pcap_compile(ph, &bf, expr, 1, PCAP_NETMASK_UNKNOWN) == -1)
			errx(1, "%s", pcap_geterr(ph));

		pcap_close(ph);

		if (argc != 1)
			free(expr);
	} else {
		insns[0].k = bd->bd_snaplen;
		bf.bf_insns = insns;
		bf.bf_len = nitems(insns);
	}

	bif = bif0;
	while ((bif = TAILQ_NEXT(bif, bif_entry)) != NULL) {
		bpfif_open(bif); /* err on failure */

		if (ioctl(bif->bif_bpf, BIOCSDLT, &bd->bd_dlt) == -1) {
			err(1, "%s: unsupported datalink type %s",
			    bif->bif_name, bd->bd_dlt_name);
		}
	}

	waittv.tv_sec = waitms / 1000;
	waittv.tv_usec = (waitms % 1000) * 1000;

	TAILQ_FOREACH(bif, &bd->bd_bif_list, bif_entry) {
		if (ioctl(bif->bif_bpf, BIOCSETF, &bf) == -1)
			err(1, "%s: set filter", bif0->bif_name);

		if (promisc) {
			if (ioctl(bif->bif_bpf, BIOCPROMISC, NULL) == -1)
				err(1, "%s: enable promisc", bif0->bif_name);
		}

		if (ioctl(bif->bif_bpf, BIOCSWTIMEOUT, &waittv) == -1)
			err(1, "%s: wait ms %d", bif0->bif_name, waitms);
	}

	if (bf.bf_insns != insns)
		pcap_freecode(&bf);

	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		errx(1, "can't drop privileges");

	bd->bd_fd = bpflog_open(bd);
	if (bd->bd_fd == -1) {
		/* error has already been printed */
		exit(1);
	}

	if (!debug) {
		extern char *__progname;

		if (daemon(0, 0) == -1)
			err(1, "unable to daemonize");

		logger_syslog(__progname);
	}

	if (unveil(bd->bd_fname, "rwc") == -1)
		lerr(1, "unveil %s", bd->bd_fname);
	if (unveil(NULL, NULL) == -1)
		lerr(1, "unveil");

	event_init();

	signal_set(&bd->bd_sighup, SIGHUP, bpflogd_hup, bd);
	signal_add(&bd->bd_sighup, NULL);

	TAILQ_FOREACH(bif, &bd->bd_bif_list, bif_entry) {
		event_set(&bif->bif_ev, bif->bif_bpf, EV_READ | EV_PERSIST,
		    bpfif_read, bif);
		event_add(&bif->bif_ev, NULL);
	}

	event_dispatch();

	return (0);
}

static int
bpf_maxbufsize(void)
{
	int mib[] = { CTL_NET, PF_BPF, NET_BPF_MAXBUFSIZE };
	int maxbuf;
	size_t maxbufsize = sizeof(maxbuf);

	if (sysctl(mib, nitems(mib), &maxbuf, &maxbufsize, NULL, 0) == -1)
		return (-1);

	if (maxbuf > 1 << 20)
		maxbuf = 1 << 20;

	return (maxbuf);
}

static void
bpflogd_hup(int nil, short events, void *arg)
{
	struct bpflogd *bd = arg;
	struct bpfif *bif;
	int fd;

	fd = bpflog_open(bd);

	TAILQ_FOREACH(bif, &bd->bd_bif_list, bif_entry)
		bpfif_read(bif->bif_bpf, 0, bif);

	close(bd->bd_fd);

	if (fd == -1)
		lerrx(1, "exiting");

	bd->bd_fd = fd;

	linfo("%s turned over", bd->bd_fname);
}

static int
bpflog_open(struct bpflogd *bd)
{
	const struct pcap_file_header pfh = {
		.magic = 0xa1b2c3d4,
		.version_major = PCAP_VERSION_MAJOR,
		.version_minor = PCAP_VERSION_MINOR,
		.thiszone = 0, /* we work in UTC */
		.sigfigs = 0,
		.snaplen = BPF_MAXBUFSIZE,
		.linktype = bd->bd_dlt,
	};
	struct pcap_file_header epfh;
	struct stat st;
	ssize_t rv;
	int fd;

	fd = open(bd->bd_fname, O_RDWR|O_APPEND);
	if (fd == -1) {
		lwarn("%s", bd->bd_fname);
		return (-1);
	}

	if (fstat(fd, &st) == -1) {
		lwarn("%s stat", bd->bd_fname);
		goto close;
	}

	if (st.st_size == 0) {
		rv = write(fd, &pfh, sizeof(pfh));
		if (rv == -1) {
			lwarn("%s pcap file header write", bd->bd_fname);
			goto close;
		}
		if ((size_t)rv < sizeof(pfh)) {
			lwarnx("%s pcap file header short write",
			    bd->bd_fname);
			goto close;
		}

		return (fd);
	}

	rv = pread(fd, &epfh, sizeof(epfh), 0);
	if (rv == -1) {
		lwarn("%s pcap file header read", bd->bd_fname);
		goto close;
	}
	if ((size_t)rv < sizeof(epfh)) {
		lwarn("%s pcap file header is short", bd->bd_fname);
		goto close;
	}

	if (epfh.magic != pfh.magic) {
		lwarnx("%s pcap file header magic is wrong",
		    bd->bd_fname);
		goto close;
	}
	if (epfh.version_major != pfh.version_major ||
	    epfh.version_minor < pfh.version_minor) {
		lwarnx("%s pcap file header version is unsupported",
		    bd->bd_fname);
		goto close;
	}
	if (epfh.thiszone != pfh.thiszone) {
		lwarnx("%s pcap file timezone is different", bd->bd_fname);
		goto close;
	}
	if (epfh.snaplen < bd->bd_snaplen) {
		lwarnx("%s pcap file snaplen is too short", bd->bd_fname);
		goto close;
	}
	if (epfh.linktype != pfh.linktype) {
		lwarnx("%s pcap file linktype is different", bd->bd_fname);
		goto close;
	}

	return (fd);

close:
	close(fd);
	return (-1);
}

static void
bpfif_open(struct bpfif *bif)
{
	struct bpflogd *bd = bif->bif_bd;
	struct ifreq ifr;

	bif->bif_bpf = open(_DEV_BPF, O_RDWR | O_NONBLOCK);
	if (bif->bif_bpf == -1)
		err(1, "%s: open %s", bif->bif_name, _DEV_BPF);

	memset(&ifr, 0, sizeof(ifr));
	if (strlcpy(ifr.ifr_name, bif->bif_name, sizeof(ifr.ifr_name)) >=
	    sizeof(ifr.ifr_name))
		errx(1, "%s: interface name is too long", bif->bif_name);

	if (ioctl(bif->bif_bpf, BIOCSBLEN, &bd->bd_buflen) == -1) {
		err(1, "%s: set buffer length %d", bif->bif_name,
		    bd->bd_buflen);
	}

	if (ioctl(bif->bif_bpf, BIOCSETIF, &ifr) == -1)
		err(1, "%s: set bpf interface", bif->bif_name);
}

#define PCAP_PKTS	64

static void
bpfif_read(int fd, short events, void *arg)
{
	struct bpfif *bif = arg;
	struct bpflogd *bd = bif->bif_bd;
	ssize_t rv;
	size_t len, bpflen, caplen;
	uint8_t *buf;

	struct pcap_pkthdr pps[PCAP_PKTS], *pp;
	struct iovec iovs[PCAP_PKTS * 2], *iov = iovs;
	size_t np = 0;

	rv = read(fd, bd->bd_buf, bd->bd_buflen);
	switch (rv) {
	case -1:
		switch (errno) {
		case EINTR:
		case EAGAIN:
			break;
		default:
			lerr(1, "%s bpf read", bif->bif_name);
			/* NOTREACHED */
		}
		return;
	case 0:
		/* bpf buffer is empty */
		return;
	default:
		break;
	}

	buf = bd->bd_buf;
	len = rv;

	for (;;) {
		const struct bpf_hdr *bh;

		/* the kernel lied to us.  */
		if (len < sizeof(*bh))
			lerrx(1, "%s: short bpf header", bif->bif_name);

		bh = (const struct bpf_hdr *)buf;
		bpflen = bh->bh_hdrlen + bh->bh_caplen;

		/*
		 * If the bpf header plus data doesn't fit in what's
		 * left of the buffer, we've got a problem...
		 */
		if (bpflen > len)
			lerrx(1, "%s: short bpf read", bif->bif_name);

		if (np >= PCAP_PKTS) {
			rv = writev(bd->bd_fd, iovs, iov - iovs);
			if (rv == -1)
				lwarn("%s write", bd->bd_fname);
			iov = iovs;
			np = 0;
		}

		caplen = bh->bh_caplen;
		if (caplen > BPF_MAXBUFSIZE)
			caplen = BPF_MAXBUFSIZE;

		pp = &pps[np++];

		pp->ts = bh->bh_tstamp;
		pp->caplen = caplen;
		pp->len = bh->bh_datalen;

		iov->iov_base = pp;
		iov->iov_len = sizeof(*pp);
		iov++;
		iov->iov_base = buf + bh->bh_hdrlen;
		iov->iov_len = caplen;
		iov++;

		bpflen = BPF_WORDALIGN(bpflen);
		if (len <= bpflen) {
			/* everything is consumed */
			break;
		}

		/* Move the lop to the next packet */
		buf += bpflen;
		len -= bpflen;
	}

	if (np > 0) {
		rv = writev(bd->bd_fd, iovs, iov - iovs);
		if (rv == -1)
			lwarn("%s write", bd->bd_fname);
	}

	fsync(bd->bd_fd);
}
