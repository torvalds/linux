/*	$OpenBSD: tcpdump.c,v 1.100 2025/05/16 05:47:30 kn Exp $	*/

/*
 * Copyright (c) 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997
 *	The Regents of the University of California.  All rights reserved.
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
 * tcpdump - monitor tcp/ip traffic on an ethernet.
 *
 * First written in 1987 by Van Jacobson, Lawrence Berkeley Laboratory.
 * Mercilessly hacked and occasionally improved since then via the
 * combined efforts of Van, Steve McCanne and Craig Leres of LBL.
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#include <netinet/in.h>

#include <pcap.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>

#include "interface.h"
#include "addrtoname.h"
#include "setsignal.h"
#include "gmt2local.h"

#include <sys/socket.h>
#include <net/if.h>
#include <net/pfvar.h>
#include "extract.h"
#include "pfctl.h"
#include "pfctl_parser.h"
#include "privsep.h"

int Aflag;			/* dump ascii */
int aflag;			/* translate network and broadcast addresses */
int Bflag = BPF_FILDROP_PASS;	/* BPF fildrop setting */
int dflag;			/* print filter code */
int eflag;			/* print ethernet header */
int fflag;			/* don't translate "foreign" IP address */
int Iflag;			/* include interface in output */
int Lflag;			/* List available link types */
int nflag;			/* leave addresses as numbers */
int Nflag;			/* remove domains from printed host names */
int Oflag = 1;			/* run filter code optimizer */
int oflag;			/* print passive OS fingerprints */
int pflag;			/* don't go promiscuous */
int qflag;			/* quick (shorter) output */
int Sflag;			/* print raw TCP sequence numbers */
int tflag = 1;			/* print packet arrival time */
int vflag;			/* verbose */
int xflag;			/* print packet in hex */
int Xflag;			/* print packet in emacs-hexl style */

int packettype;

char *program_name;
char *device = NULL;

int32_t thiszone;		/* seconds offset from gmt to local time */

extern volatile pid_t child_pid;

/* Externs */
extern void bpf_dump(struct bpf_program *, int);
extern int esp_init(char *);

/* Forwards */
void	cleanup(int);
void	gotchld(int);
extern __dead void usage(void);

/* Length of saved portion of packet. */
int snaplen = 0;

struct printer {
	pcap_handler f;
	int type;
};

/* XXX needed if using old bpf.h */
#ifndef DLT_ATM_RFC1483
#define DLT_ATM_RFC1483 11
#endif

static struct printer printers[] = {
	{ ether_if_print,		DLT_EN10MB },
	{ ether_if_print,		DLT_IEEE802 },
	{ sl_if_print,			DLT_SLIP },
	{ sl_bsdos_if_print,		DLT_SLIP_BSDOS },
	{ ppp_if_print,			DLT_PPP },
	{ ppp_hdlc_if_print,		DLT_PPP_SERIAL },
	{ fddi_if_print,		DLT_FDDI },
	{ null_if_print,		DLT_NULL },
	{ raw_if_print,			DLT_RAW },
	{ atm_if_print,			DLT_ATM_RFC1483 },
	{ loop_if_print,		DLT_LOOP },
	{ enc_if_print,			DLT_ENC },
	{ pflog_if_print,		DLT_PFLOG },
	{ pfsync_if_print,		DLT_PFSYNC },
	{ ppp_ether_if_print,		DLT_PPP_ETHER },
	{ ieee802_11_if_print,		DLT_IEEE802_11 },
	{ ieee802_11_radio_if_print,	DLT_IEEE802_11_RADIO },
	{ ofp_if_print,			DLT_OPENFLOW },
	{ usbpcap_if_print,		DLT_USBPCAP },
	{ NULL,				0 },
};

static pcap_handler
lookup_printer(int type)
{
	struct printer *p;

	for (p = printers; p->f; ++p) {
		if (type == p->type)
			return p->f;
	}

	error("unknown data link type 0x%x", type);
	/* NOTREACHED */
}

static int
init_pfosfp(void)
{
	pf_osfp_initialize();
	if (pfctl_file_fingerprints(-1,
	    PF_OPT_QUIET|PF_OPT_NOACTION, PF_OSFP_FILE) == 0)
		return 1;
	return 0;
}

static pcap_t *pd;

/* Multiple DLT support */
void		 pcap_list_linktypes(pcap_t *);
void		 pcap_print_linktype(u_int);

void
pcap_print_linktype(u_int dlt)
{
	const char *name;

	if ((name = pcap_datalink_val_to_name(dlt)) != NULL)
		fprintf(stderr, "%s\n", name);
	else
		fprintf(stderr, "<unknown: %u>\n", dlt);
}

void
pcap_list_linktypes(pcap_t *p)
{
	int fd = p->fd;
	u_int n;

#define MAXDLT	100

	u_int dltlist[MAXDLT];
	struct bpf_dltlist dl = {MAXDLT, dltlist};

	if (fd < 0)
		error("Invalid bpf descriptor");

	if (ioctl(fd, BIOCGDLTLIST, &dl) == -1)
		err(1, "BIOCGDLTLIST");

	if (dl.bfl_len > MAXDLT)
		error("Invalid number of linktypes: %u", dl.bfl_len);

	fprintf(stderr, "%d link type%s supported:\n", dl.bfl_len,
	    dl.bfl_len == 1 ? "" : "s");

	for (n = 0; n < dl.bfl_len; n++) {
		fprintf(stderr, "\t");
		pcap_print_linktype(dltlist[n]);
	}
}

int
main(int argc, char **argv)
{
	int cnt = -1, op, i;
	bpf_u_int32 localnet, netmask;
	char *cp, *RFileName = NULL;
	char ebuf[PCAP_ERRBUF_SIZE], *WFileName = NULL;
	pcap_handler printer;
	struct bpf_program *fcode;
	u_char *pcap_userdata;
	u_int dirfilt = 0, dlt = (u_int) -1;
	const char *errstr;

	if ((cp = strrchr(argv[0], '/')) != NULL)
		program_name = cp + 1;
	else
		program_name = argv[0];

	/* '-P' used internally, exec privileged portion */
	if (argc >= 2 && strcmp("-P", argv[1]) == 0)
		priv_exec(argc, argv);

	if (priv_init(argc, argv))
		error("Failed to setup privsep");

	/* state: STATE_INIT */

	opterr = 0;
	while ((op = getopt(argc, argv,
	    "AaB:c:D:deE:fF:i:IlLnNOopqr:s:StT:vw:xXy:")) != -1)
		switch (op) {

		case 'A':
			xflag = 1;
			Aflag = 1;
			break;

		case 'a':
			aflag = 1;
			break;

		case 'B':
			if (strcasecmp(optarg, "pass") == 0)
				Bflag = BPF_FILDROP_PASS;
			else if (strcasecmp(optarg, "capture") == 0)
				Bflag = BPF_FILDROP_CAPTURE;
			else if (strcasecmp(optarg, "drop") == 0)
				Bflag = BPF_FILDROP_DROP;
			else {
				error("invalid BPF fildrop option: %s",
				    optarg);
			}
			break;

		case 'c':
			cnt = strtonum(optarg, 1, INT_MAX, &errstr);
			if (errstr)
				error("invalid packet count %s: %s",
				    optarg, errstr);
			break;

		case 'D':
			if (strcasecmp(optarg, "in") == 0)
				dirfilt = BPF_DIRECTION_OUT;
			else if (strcasecmp(optarg, "out") == 0)
				dirfilt = BPF_DIRECTION_IN;
			else
				error("invalid traffic direction %s", optarg);
			break;

		case 'd':
			++dflag;
			break;
		case 'e':
			eflag = 1;
			break;

		case 'f':
			fflag = 1;
			break;

		case 'F':
			break;

		case 'i':
			device = optarg;
			break;

		case 'I':
			Iflag = 1;
			break;

		case 'l':
			setvbuf(stdout, NULL, _IOLBF, 0);
			break;
		case 'L':
			Lflag = 1;
			break;
		case 'n':
			nflag = 1;
			break;

		case 'N':
			Nflag = 1;
			break;

		case 'O':
			Oflag = 0;
			break;

		case 'o':
			oflag = 1;
			break;

		case 'p':
			pflag = 1;
			break;

		case 'q':
			qflag = 1;
			break;

		case 'r':
			RFileName = optarg;
			break;

		case 's':
			snaplen = strtonum(optarg, 1, INT_MAX, &errstr);
			if (errstr)
				error("invalid snaplen %s: %s", optarg, errstr);
			break;

		case 'S':
			Sflag = 1;
			break;

		case 't':
			--tflag;
			break;

		case 'T':
			if (strcasecmp(optarg, "vat") == 0)
				packettype = PT_VAT;
			else if (strcasecmp(optarg, "wb") == 0)
				packettype = PT_WB;
			else if (strcasecmp(optarg, "rpc") == 0)
				packettype = PT_RPC;
			else if (strcasecmp(optarg, "rtp") == 0)
				packettype = PT_RTP;
			else if (strcasecmp(optarg, "rtcp") == 0)
				packettype = PT_RTCP;
			else if (strcasecmp(optarg, "cnfp") == 0)
				packettype = PT_CNFP;
			else if (strcasecmp(optarg, "vrrp") == 0)
				packettype = PT_VRRP;
			else if (strcasecmp(optarg, "tcp") == 0)
				packettype = PT_TCP;
			else if (strcasecmp(optarg, "gre") == 0)
				packettype = PT_GRE;
			else if (strcasecmp(optarg, "vxlan") == 0)
				packettype = PT_VXLAN;
			else if (strcasecmp(optarg, "geneve") == 0)
				packettype = PT_GENEVE;
			else if (strcasecmp(optarg, "erspan") == 0)
				packettype = PT_ERSPAN;
			else if (strcasecmp(optarg, "mpls") == 0)
				packettype = PT_MPLS;
			else if (strcasecmp(optarg, "tftp") == 0)
				packettype = PT_TFTP;
			else if (strcasecmp(optarg, "wg") == 0)
				packettype = PT_WIREGUARD;
			else if (strcasecmp(optarg, "sack") == 0)
				/*
				 * kept for compatibility; DEFAULT_SNAPLEN
				 * used to be too short to capture SACK.
				 */
				;
			else
				error("unknown packet type `%s'", optarg);
			break;

		case 'v':
			++vflag;
			break;

		case 'w':
			WFileName = optarg;
			break;

		case 'y':
			i = pcap_datalink_name_to_val(optarg);
			if (i < 0)
				error("invalid data link type: %s", optarg);
			dlt = (u_int)i;
			break;

		case 'x':
			xflag = 1;
			break;

		case 'X':
			Xflag = 1;
			xflag = 1;
			break;

		case 'E':
			if (esp_init(optarg) < 0)
				error("bad esp specification `%s'", optarg);
			break;

		default:
			usage();
			/* NOTREACHED */
		}

	if (snaplen == 0) {
		switch (dlt) {
		case DLT_IEEE802_11:
			snaplen = IEEE802_11_SNAPLEN;
			break;
		case DLT_IEEE802_11_RADIO:
			snaplen = IEEE802_11_RADIO_SNAPLEN;
			break;
		default:
			snaplen = DEFAULT_SNAPLEN;
			break;
		}
	}

	if (aflag && nflag)
		error("-a and -n options are incompatible");

	if (RFileName != NULL) {
		pd = priv_pcap_offline(RFileName, ebuf);
		if (pd == NULL)
			error("%s", ebuf);
		/* state: STATE_BPF */
		localnet = 0;
		netmask = 0;
		if (fflag != 0)
			error("-f and -r options are incompatible");
	} else {
		if (device == NULL) {
			device = pcap_lookupdev(ebuf);
			if (device == NULL)
				error("%s", ebuf);
		}
		pd = priv_pcap_live(device, snaplen, !pflag, 1000, ebuf,
		    dlt, dirfilt, Bflag);
		if (pd == NULL)
			error("%s", ebuf);

		/* state: STATE_BPF */
		if (pcap_lookupnet(device, &localnet, &netmask, ebuf)) {
			if (fflag)
				warning("%s", ebuf);
			localnet = 0;
			netmask = 0;
		}
	}
	i = pcap_snapshot(pd);
	if (snaplen < i) {
		warning("snaplen raised from %d to %d", snaplen, i);
		snaplen = i;
	}

	if (Lflag) {
		pcap_list_linktypes(pd);
		exit(0);
	}

	fcode = priv_pcap_setfilter(pd, Oflag, netmask);
	/* state: STATE_FILTER */
	if (fcode == NULL)
		error("%s", pcap_geterr(pd));
	if (dflag) {
		bpf_dump(fcode, dflag);
		exit(0);
	}
	if (oflag)
		oflag = init_pfosfp();
	init_addrtoname(localnet, netmask);

	if (WFileName) {
		pcap_dumper_t *p;

		p = priv_pcap_dump_open(pd, WFileName);
		/* state: STATE_RUN */
		if (p == NULL)
			error("%s", pcap_geterr(pd));
		{
			FILE *fp = (FILE *)p;	/* XXX touching pcap guts! */
			fflush(fp);
			setvbuf(fp, NULL, _IONBF, 0);
		}
		printer = pcap_dump;
		pcap_userdata = (u_char *)p;
	} else {
		printer = lookup_printer(pcap_datalink(pd));
		pcap_userdata = NULL;
		priv_init_done();
		/* state: STATE_RUN */
	}
	if (RFileName == NULL) {
		(void)fprintf(stderr, "%s: listening on %s, link-type ",
		    program_name, device);
		pcap_print_linktype(pd->linktype);
		(void)fflush(stderr);
	}

	if (tflag > 0)
		thiszone = gmt2local(0);

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	if (pcap_loop(pd, cnt, printer, pcap_userdata) < 0) {
		(void)fprintf(stderr, "%s: pcap_loop: %s\n",
		    program_name, pcap_geterr(pd));
		exit(1);
	}
	pcap_close(pd);
	exit(0);
}

/* make a clean exit on interrupts */
void
cleanup(int signo)
{
	struct pcap_stat stat;
	sigset_t allsigs;

	sigfillset(&allsigs);
	sigprocmask(SIG_BLOCK, &allsigs, NULL);

	/* Can't print the summary if reading from a savefile */
	dprintf(STDERR_FILENO, "\n");
	if (pd != NULL && pcap_file(pd) == NULL) {
		if (priv_pcap_stats(&stat) < 0) {
			dprintf(STDERR_FILENO,
			    "pcap_stats: %s\n", pcap_geterr(pd));
		} else {
			dprintf(STDERR_FILENO,
			    "%u packets received by filter\n", stat.ps_recv);
			dprintf(STDERR_FILENO,
			    "%u packets dropped by kernel\n", stat.ps_drop);
		}
	}
	_exit(0);
}

void
gotchld(int signo)
{
	pid_t pid;
	int status;
	int save_err = errno;

	do {
		pid = waitpid(child_pid, &status, WNOHANG);
		if (pid > 0 && (WIFEXITED(status) || WIFSIGNALED(status)))
			cleanup(0);
	} while (pid == -1 && errno == EINTR);

	if (pid == -1)
		_exit(1);

	errno = save_err;
}

/* dump the buffer in `emacs-hexl' style */
void
default_print_hexl(const u_char *cp, unsigned int length)
{
	unsigned int i, j, jm;
	int c;
	char ln[128], buf[128];

	printf("\n");
	for (i = 0; i < length; i += 0x10) {
		snprintf(ln, sizeof(ln), "  %04x: ", (unsigned int)i);
		jm = length - i;
		jm = jm > 16 ? 16 : jm;

		for (j = 0; j < jm; j++) {
			if ((j % 2) == 1)
				snprintf(buf, sizeof(buf), "%02x ",
				    (unsigned int)cp[i+j]);
			else
				snprintf(buf, sizeof(buf), "%02x",
				    (unsigned int)cp[i+j]);
			strlcat(ln, buf, sizeof ln);
		}
		for (; j < 16; j++) {
			if ((j % 2) == 1)
				snprintf(buf, sizeof buf, "   ");
			else
				snprintf(buf, sizeof buf, "  ");
			strlcat(ln, buf, sizeof ln);
		}

		strlcat(ln, " ", sizeof ln);
		for (j = 0; j < jm; j++) {
			c = cp[i+j];
			c = isprint(c) ? c : '.';
			buf[0] = c;
			buf[1] = '\0';
			strlcat(ln, buf, sizeof ln);
		}
		printf("%s\n", ln);
	}
}

/* dump the text from the buffer */
void
default_print_ascii(const u_char *cp, unsigned int length)
{
	int c, i;

	printf("\n");
	for (i = 0; i < length; i++) {
		c = cp[i];
		if (isprint(c) || c == '\t' || c == '\n' || c == '\r')
			putchar(c);
		else
			putchar('.');
	}
}

void
default_print(const u_char *bp, u_int length)
{
	u_int i;
	int nshorts;

	if (snapend - bp < length)
		length = snapend - bp;

	if (Xflag) {
		/* dump the buffer in `emacs-hexl' style */
		default_print_hexl(bp, length);
	} else if (Aflag) {
		/* dump the text in the buffer */
		default_print_ascii(bp, length);
	} else {
		u_short sp;

		/* dump the buffer in old tcpdump style */
		nshorts = (u_int) length / sizeof(u_short);
		i = 0;
		while (--nshorts >= 0) {
			if ((i++ % 8) == 0)
				printf("\n\t\t\t");

			sp = EXTRACT_16BITS(bp);
			bp += sizeof(sp);
			printf(" %04x", sp);
		}
		if (length & 1) {
			if ((i % 8) == 0)
				printf("\n\t\t\t");
			printf(" %02x", *bp);
		}
	}
}

void
set_slave_signals(void)
{
	setsignal(SIGTERM, cleanup);
	setsignal(SIGINT, cleanup);
	setsignal(SIGCHLD, gotchld);
	setsignal(SIGHUP, cleanup);
}

__dead void
usage(void)
{
	(void)fprintf(stderr,
"Usage: %s [-AadefILlNnOopqStvXx] [-B fildrop] [-c count] [-D direction]\n",
	    program_name);
	(void)fprintf(stderr,
"\t       [-E [espalg:]espkey] [-F file] [-i interface] [-r file]\n");
	(void)fprintf(stderr,
"\t       [-s snaplen] [-T type] [-w file] [-y datalinktype] [expression ...]\n");
	exit(1);
}
