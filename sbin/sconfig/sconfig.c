/*
 * Channel configuration utility for Cronyx serial adapters.
 *
 * Copyright (C) 1997-2002 Cronyx Engineering.
 * Author: Serge Vakulenko, <vak@cronyx.ru>
 *
 * Copyright (C) 1999-2005 Cronyx Engineering.
 * Author: Roman Kurakin, <rik@cronyx.ru>
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations permission to use
 * or modify this software as long as this message is kept with the software,
 * all derivative works or modified versions.
 *
 * Cronyx Id: sconfig.c,v 1.4.2.2 2005/11/09 13:01:35 rik Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <net/if.h>
#include <machine/cserial.h>

#define MAXCHAN 128

int vflag, eflag, sflag, mflag, cflag, fflag, iflag, aflag, xflag;
int tflag, uflag;
char mask[64];
int adapter_type;		/* 0-sigma, 1-tau, 2-taupci, 3-tau32 */
char chan_name[16];

static void
usage (void)
{
	printf(
"Serial Adapter Configuration Utility\n"
"Copyright (C) 1998-2005 Cronyx Engineering.\n"
"See also man sconfig (8)\n"
"Usage:\n"
"\tsconfig [-aimsxeftuc] [device [parameters ...]]\n"
"\n"
"Options:\n"
"\t<no options>\t\t -- print channel options\n"
"\t-a\t\t\t -- print all settings of the channel\n"
"\t-i\t\t\t -- print network interface status\n"
"\t-m\t\t\t -- print modem signal status\n"
"\t-s\t\t\t -- print channel statistics\n"
"\t-x\t\t\t -- print extended channel statistics\n"
"\t-e\t\t\t -- print short E1/G703 statistics\n"
"\t-f\t\t\t -- print full E1/G703 statistics\n"
"\t-t\t\t\t -- print short E3/T3/STS-1 statistics\n"
"\t-u\t\t\t -- print full E3/T3/STS-1 statistics\n"
"\t-c\t\t\t -- clear statistics\n"
"\nParameters:\n"
"\t<number>\t\t -- baud rate, internal clock\n"
"\textclock\t\t -- external clock (default)\n"
"\nProtocol options:\n"
"\tasync\t\t\t -- asynchronous protocol\n"
#ifdef __linux__
"\tsync\t\t\t -- synchronous protocol\n"
#endif
"\tcisco\t\t\t -- Cisco/HDLC protocol\n"
"\tfr\t\t\t -- Frame Relay protocol\n"
#ifdef __linux__
"\t    dlci<number>\t -- Add new DLCI\n"
#endif
"\tppp\t\t\t -- PPP protocol\n"
#ifdef __linux__
"\trbrg\t\t\t -- Remote bridge\n"
"\traw\t\t\t -- raw HDLC protocol\n"
"\tpacket\t\t\t -- packetized HDLC protocol\n"
"\tidle\t\t\t -- no protocol\n"
#else
"\t    keepalive={on,of}\t -- Enable/disable keepalive\n"
#endif
"\nInterface options:\n"
"\tport={rs232,v35,rs449}\t -- port type (for old models of Sigma)\n"
"\tcfg={A,B,C}\t\t -- adapter configuration\n"
"\tloop={on,off}\t\t -- internal loopback\n"
"\trloop={on,off}\t\t -- remote loopback\n"
"\tdpll={on,off}\t\t -- DPLL mode\n"
"\tnrzi={on,off}\t\t -- NRZI encoding\n"
"\tinvclk={on,off}\t\t -- invert receive and transmit clock\n"
"\tinvrclk={on,off}\t -- invert receive clock\n"
"\tinvtclk={on,off}\t -- invert transmit clock\n"
"\thigain={on,off}\t\t -- E1 high non linear input sensitivity \n\t\t\t\t    (long line)\n"
"\tmonitor={on,off}\t -- E1 high linear input sensitivity \n\t\t\t\t    (interception mode)\n"
"\tphony={on,off}\t\t -- E1 telepnony mode\n"
"\tunfram={on,off}\t\t -- E1 unframed mode\n"
"\tscrambler={on,off}\t -- G.703 scrambling mode\n"
"\tuse16={on,off}\t\t -- E1 timeslot 16 usage\n"
"\tcrc4={on,off}\t\t -- E1 CRC4 mode\n"
#ifdef __linux__
"\tami={on,off}\t\t -- E1 AMI or HDB3 line code\n"
"\tmtu={size}\t\t -- set MTU in bytes\n"
#endif
"\tsyn={int,rcv,rcvX}\t -- G.703 transmit clock\n"
"\tts=...\t\t\t -- E1 timeslots\n"
"\tpass=...\t\t -- E1 subchannel timeslots\n"
"\tdir=<num>\t\t -- connect channel to link<num>\n"
/*"\trqken={size}\t\t -- set receive queue length in packets\n"*/
/*"\tcablen={on,off}\t\t -- T3/STS-1 high transmitter output for long cable\n"*/
"\tdebug={0,1,2}\t\t -- enable/disable debug messages\n"
	);
	exit (0);
}

static unsigned long
scan_timeslots (char *s)
{
	char *e;
	long v;
	int i;
	unsigned long ts, lastv;

	ts = lastv = 0;
	for (;;) {
		v = strtol (s, &e, 10);
		if (e == s)
			break;
		if (*e == '-') {
			lastv = v;
			s = e+1;
			continue;
		}
		if (*e == ',')
			++e;

		if (lastv)
			for (i=lastv; i<v; ++i)
				ts |= 1L << i;
		ts |= 1L << v;

		lastv = 0;
		s = e;
	}
	return ts;
}

static int
ppp_ok (void)
{
#ifdef __linux__
	int s, p;
	struct ifreq ifr;
	char pttyname[32];
	char *p1, *p2;
	int i, j;
	int ppp_disc = N_PPP;

	/*
	 * Open a socket for doing the ioctl operations.
	 */
	s = socket (AF_INET, SOCK_DGRAM, 0);
	if (s < 0) {
		fprintf (stderr, "Error opening socket.\n");
		return 0;
	}
	strncpy (ifr.ifr_name, "ppp0", sizeof (ifr.ifr_name));
	if (ioctl (s, SIOCGIFFLAGS, (caddr_t) &ifr) >= 0) {
		/* Ok. */
		close (s);
		return 1;
	}
	close (s);

	/* open pseudo-tty and try to set PPP discipline */
	sprintf (pttyname, "/dev/ptyXX");
	p1 = &pttyname[8];
	p2 = &pttyname[9];
	for (i=0; i<16; i++) {
		struct stat stb;

		*p1 = "pqrstuvwxyzabcde"[i];
		*p2 = '0';
		if (stat (pttyname, &stb) < 0)
			continue;
		for (j=0; j<16; j++) {
			*p2 = "0123456789abcdef"[j];
			p = open (pttyname, 2);
			if (p > 0) {
				if (ioctl (p, TIOCSETD, &ppp_disc) < 0) {
					fprintf (stderr, "No PPP discipline in kernel.\n");
					close (p);
					return 0;
				}
				close (p);
				return 1;
			}
		}
	}
	fprintf (stderr, "Cannot get pseudo-tty.\n");
	return 0;
#else
	return 1;
#endif
}

static char *
format_timeslots (unsigned long s)
{
	static char buf [100];
	char *p = buf;
	int i;

	for (i=1; i<32; ++i)
		if ((s >> i) & 1) {
			int prev = (i > 1)  & (s >> (i-1));
			int next = (i < 31) & (s >> (i+1));

			if (prev) {
				if (next)
					continue;
				*p++ = '-';
			} else if (p > buf)
				*p++ = ',';

			if (i >= 10)
				*p++ = '0' + i / 10;
			*p++ = '0' + i % 10;
		}
	*p = 0;
	return buf;
}

static void
print_modems (int fd, int need_header)
{
	int status;

	if (ioctl (fd, TIOCMGET, &status) < 0) {
		perror ("getting modem status");
		return;
	}
	if (need_header)
		printf ("Channel\tLE\tDTR\tDSR\tRTS\tCTS\tCD\n");
	printf ("%s\t%s\t%s\t%s\t%s\t%s\t%s\n", chan_name,
		status & TIOCM_LE  ? "On" : "-",
		status & TIOCM_DTR ? "On" : "-",
		status & TIOCM_DSR ? "On" : "-",
		status & TIOCM_RTS ? "On" : "-",
		status & TIOCM_CTS ? "On" : "-",
		status & TIOCM_CD  ? "On" : "-");
}

static void
#ifdef __linux__
print_ifconfig (int fd)
#else
print_ifconfig (int fd __unused)
#endif
{
	char buf [64];
#ifdef __linux__
	char protocol [8];

	if (ioctl (fd, SERIAL_GETPROTO, &protocol) >= 0 &&
	    strcmp (protocol, "fr") == 0)
		sprintf (buf, "ifconfig %sd16 2>/dev/null", chan_name);
	else
#endif
	sprintf (buf, "ifconfig %s 2>/dev/null", chan_name);
	system (buf);
}

static void
set_debug_ifconfig (int on)
{
	char buf [64];
	sprintf (buf, "ifconfig %s %sdebug 2>/dev/null", chan_name,
		 on ? "" : "-");
	system (buf);
}

static char *
format_long (unsigned long val)
{
	static char s[32];
	int l;
	l = sprintf (s, "%lu", val);
	if (l>7 && !sflag) {
		s[3] = s[2];
		s[2] = s[1];
		s[1] = '.';
		s[4] = 'e';
		sprintf (s + 5, "%02d", l-1);
	}
	return s;
}

static void
print_stats (int fd, int need_header)
{
	struct serial_statistics st;
	unsigned long sarr [9];
	int i;

	if (ioctl (fd, SERIAL_GETSTAT, &st) < 0) {
		perror ("getting statistics");
		return;
	}
	if (need_header) {
		if (sflag) {
			printf ("        ------------Receive-----------      "
				"------------Transmit----------\n");
			printf ("Channel Interrupts  Packets     Errors      "
				"Interrupts  Packets     Errors\n");
		}
		else    {
			printf ("        --------Receive---------------  "
				"--------Transmit--------------  Modem\n");
			printf ("Channel Intrs   Bytes   Packets Errors  "
				"Intrs   Bytes   Packets Errors  Intrs\n");
		}
	}
	
	sarr [0] = st.rintr;
	sarr [1] = st.ibytes;
	sarr [2] = st.ipkts;
	sarr [3] = st.ierrs;
	sarr [4] = st.tintr;
	sarr [5] = st.obytes;
	sarr [6] = st.opkts;
	sarr [7] = st.oerrs;
	sarr [8] = st.mintr;
	printf ("%s", chan_name);
	if (sflag) {
		printf ("\t%-12lu%-12lu%-12lu%-12lu%-12lu%-12lu", sarr[0],
			sarr[2], sarr[3], sarr[4], sarr[6], sarr[7]);
	} else {
		for (i = 0; i < 9; i++)
			printf ("\t%s", format_long (sarr [i]));
		printf ("\n");
	}
}

static void
clear_stats (int fd)
{
	if (ioctl (fd, SERIAL_CLRSTAT, 0) < 0) {
		perror ("clearing statistics");
		exit (-1);
	}
}

static char *
format_e1_status (unsigned long status)
{
	static char buf [80];

	if (status == 0)
		return "n/a";
	if (status & E1_NOALARM)
		return "Ok";
	buf[0] = 0;
	if (status & E1_LOS)     strcat (buf, ",LOS");
	if (status & E1_AIS)     strcat (buf, ",AIS");
	if (status & E1_LOF)     strcat (buf, ",LOF");
	if (status & E1_LOMF)    strcat (buf, ",LOMF");
	if (status & E1_CRC4E)   strcat (buf, ",CRC4E");
	if (status & E1_FARLOF)  strcat (buf, ",FARLOF");
	if (status & E1_AIS16)   strcat (buf, ",AIS16");
	if (status & E1_FARLOMF) strcat (buf, ",FARLOMF");
/*	if (status & E1_TSTREQ)  strcat (buf, ",TSTREQ");*/
/*	if (status & E1_TSTERR)  strcat (buf, ",TSTERR");*/
	if (buf[0] == ',')
		return buf+1;
	return "Unknown";
}

static void
print_frac (int leftalign, unsigned long numerator, unsigned long divider)
{
	int n;

	if (numerator < 1 || divider < 1) {
		printf (leftalign ? "/-   " : "    -");
		return;
	}
	n = (int) (0.5 + 1000.0 * numerator / divider);
	if (n < 1000) {
		printf (leftalign ? "/.%-3d" : " .%03d", n);
		return;
	}
	putchar (leftalign ? '/' : ' ');

	if      (n >= 1000000) n = (n+500) / 1000 * 1000;
	else if (n >= 100000)  n = (n+50)  / 100 * 100;
	else if (n >= 10000)   n = (n+5)   / 10 * 10;

	switch (n) {
	case 1000:    printf (".999"); return;
	case 10000:   n = 9990;   break;
	case 100000:  n = 99900;  break;
	case 1000000: n = 999000; break;
	}
	if (n < 10000)        printf ("%d.%d", n/1000, n/10%100);
	else if (n < 100000)  printf ("%d.%d", n/1000, n/100%10);
	else if (n < 1000000) printf ("%d.", n/1000);
	else                  printf ("%d", n/1000);
}

static void
print_e1_stats (int fd, int need_header)
{
	struct e1_statistics st;
	int i, maxi;

	if (need_header)
		printf ("Chan\t Unav/Degr  Bpv/Fsyn  CRC/RCRC  Err/Lerr  Sev/Bur   Oof/Slp  Status\n");

	if (ioctl (fd, SERIAL_GETESTAT, &st) < 0)
		return;
	printf ("%s\t", chan_name);

	/* Unavailable seconds, degraded minutes */
	print_frac (0, st.currnt.uas, st.cursec);
	print_frac (1, 60 * st.currnt.dm, st.cursec);

	/* Bipolar violations, frame sync errors */
	print_frac (0, st.currnt.bpv, st.cursec);
	print_frac (1, st.currnt.fse, st.cursec);

	/* CRC errors, remote CRC errors (E-bit) */
	print_frac (0, st.currnt.crce, st.cursec);
	print_frac (1, st.currnt.rcrce, st.cursec);

	/* Errored seconds, line errored seconds */
	print_frac (0, st.currnt.es, st.cursec);
	print_frac (1, st.currnt.les, st.cursec);

	/* Severely errored seconds, bursty errored seconds */
	print_frac (0, st.currnt.ses, st.cursec);
	print_frac (1, st.currnt.bes, st.cursec);

	/* Out of frame seconds, controlled slip seconds */
	print_frac (0, st.currnt.oofs, st.cursec);
	print_frac (1, st.currnt.css, st.cursec);

	printf (" %s\n", format_e1_status (st.status));

	if (fflag) {
		/* Print total statistics. */
		printf ("\t");
		print_frac (0, st.total.uas, st.totsec);
		print_frac (1, 60 * st.total.dm, st.totsec);

		print_frac (0, st.total.bpv, st.totsec);
		print_frac (1, st.total.fse, st.totsec);

		print_frac (0, st.total.crce, st.totsec);
		print_frac (1, st.total.rcrce, st.totsec);

		print_frac (0, st.total.es, st.totsec);
		print_frac (1, st.total.les, st.totsec);

		print_frac (0, st.total.ses, st.totsec);
		print_frac (1, st.total.bes, st.totsec);

		print_frac (0, st.total.oofs, st.totsec);
		print_frac (1, st.total.css, st.totsec);

		printf (" -- Total\n");

		/* Print 24-hour history. */
		maxi = (st.totsec - st.cursec) / 900;
		if (maxi > 48)
			maxi = 48;
		for (i=0; i<maxi; ++i) {
			printf ("       ");
			print_frac (0, st.interval[i].uas, 15*60);
			print_frac (1, 60 * st.interval[i].dm, 15*60);

			print_frac (0, st.interval[i].bpv, 15*60);
			print_frac (1, st.interval[i].fse, 15*60);

			print_frac (0, st.interval[i].crce, 15*60);
			print_frac (1, st.interval[i].rcrce, 15*60);

			print_frac (0, st.interval[i].es, 15*60);
			print_frac (1, st.interval[i].les, 15*60);

			print_frac (0, st.interval[i].ses, 15*60);
			print_frac (1, st.interval[i].bes, 15*60);

			print_frac (0, st.interval[i].oofs, 15*60);
			print_frac (1, st.interval[i].css, 15*60);

			if (i < 3)
				printf (" -- %dm\n", (i+1)*15);
			else
				printf (" -- %dh %dm\n", (i+1)/4, (i+1)%4*15);
		}
	}
}

static char *
format_e3_status (unsigned long status)
{
	static char buf [80];

	buf[0] = 0;
	if (status & E3_LOS)     strcat (buf, ",LOS");
	if (status & E3_TXE)     strcat (buf, ",XMIT");
	if (buf[0] == ',')
		return buf+1;
	return "Ok";
}

static char *
format_e3_cv (unsigned long cv, unsigned long baud, unsigned long atime)
{
	static char buf[80];
	
	if (!cv || !baud || !atime)
		sprintf (buf, "         -         ");
	else
		sprintf (buf, "%10lu (%.1e)", cv, (double)cv/baud/atime);
	return buf;
}

static void
print_e3_stats (int fd, int need_header)
{
	struct e3_statistics st;
	int i, maxi;
	long baud;

	if (need_header)
		printf ("Chan\t--Code Violations---\t\t\t\t\t ----Status----\n");

	if (ioctl (fd, SERIAL_GETE3STAT, &st) < 0 ||
	    ioctl (fd, SERIAL_GETBAUD, &baud) < 0)
		return;
		
	if (!st.cursec)
		st.cursec = 1;

	printf ("%s\t%s\t\t\t\t\t", chan_name,
		format_e3_cv (st.ccv, baud, st.cursec));

	printf (" %s\n", format_e3_status (st.status));


	if (uflag) {
		/* Print total statistics. */
		printf ("\t%s\t\t\t\t\t",
			format_e3_cv (st.tcv, baud, st.totsec));
		printf (" -- Total\n");

		/* Print 24-hour history. */
		maxi = (st.totsec - st.cursec) / 900;
		if (maxi > 48)
			maxi = 48;
		for (i=0; i<maxi; ++i) {
			printf ("\t%s\t\t\t\t\t",
				format_e3_cv (st.icv[i], baud, 15*60));
			if (i < 3)
				printf (" -- %2dm\n", (i+1)*15);
			else
				printf (" -- %2dh %2dm\n", (i+1)/4, (i+1)%4*15);
		}
	}
}

static void
print_chan (int fd)
{
	char protocol [8];
	char cfg;
	int loop, dpll, nrzi, invclk, clk, higain, phony, use16, crc4;
	int level, keepalive, debug, port, invrclk, invtclk, unfram, monitor;
	int cable, dir, scrambler, ami, mtu;
	int cablen, rloop, rqlen;
	long baud, timeslots, subchan;
	int protocol_valid, baud_valid, loop_valid, use16_valid, crc4_valid;
	int dpll_valid, nrzi_valid, invclk_valid, clk_valid, phony_valid;
	int timeslots_valid, subchan_valid, higain_valid, level_valid;
	int keepalive_valid, debug_valid, cfg_valid, port_valid;
	int invrclk_valid, invtclk_valid, unfram_valid, monitor_valid;
	int cable_valid, dir_valid, scrambler_valid, ami_valid, mtu_valid;
	int cablen_valid, rloop_valid, rqlen_valid;

	protocol_valid  = ioctl (fd, SERIAL_GETPROTO, &protocol) >= 0;
	cfg_valid       = ioctl (fd, SERIAL_GETCFG, &cfg) >= 0;
	baud_valid      = ioctl (fd, SERIAL_GETBAUD, &baud) >= 0;
	loop_valid      = ioctl (fd, SERIAL_GETLOOP, &loop) >= 0;
	dpll_valid      = ioctl (fd, SERIAL_GETDPLL, &dpll) >= 0;
	nrzi_valid      = ioctl (fd, SERIAL_GETNRZI, &nrzi) >= 0;
	invclk_valid    = ioctl (fd, SERIAL_GETINVCLK, &invclk) >= 0;
	invrclk_valid	= ioctl (fd, SERIAL_GETINVRCLK, &invrclk) >= 0;
	invtclk_valid	= ioctl (fd, SERIAL_GETINVTCLK, &invtclk) >= 0;
	clk_valid       = ioctl (fd, SERIAL_GETCLK, &clk) >= 0;
	timeslots_valid = ioctl (fd, SERIAL_GETTIMESLOTS, &timeslots) >= 0;
	subchan_valid   = ioctl (fd, SERIAL_GETSUBCHAN, &subchan) >= 0;
	higain_valid    = ioctl (fd, SERIAL_GETHIGAIN, &higain) >= 0;
	phony_valid     = ioctl (fd, SERIAL_GETPHONY, &phony) >= 0;
	unfram_valid    = ioctl (fd, SERIAL_GETUNFRAM, &unfram) >= 0;
	monitor_valid   = ioctl (fd, SERIAL_GETMONITOR, &monitor) >= 0;
	use16_valid     = ioctl (fd, SERIAL_GETUSE16, &use16) >= 0;
	crc4_valid      = ioctl (fd, SERIAL_GETCRC4, &crc4) >= 0;
	ami_valid	= ioctl (fd, SERIAL_GETLCODE, &ami) >= 0;
	level_valid     = ioctl (fd, SERIAL_GETLEVEL, &level) >= 0;
	keepalive_valid = ioctl (fd, SERIAL_GETKEEPALIVE, &keepalive) >= 0;
	debug_valid     = ioctl (fd, SERIAL_GETDEBUG, &debug) >= 0;
	port_valid	= ioctl (fd, SERIAL_GETPORT, &port) >= 0;
	cable_valid	= ioctl (fd, SERIAL_GETCABLE, &cable) >= 0;
	dir_valid	= ioctl (fd, SERIAL_GETDIR, &dir) >= 0;
	scrambler_valid	= ioctl (fd, SERIAL_GETSCRAMBLER, &scrambler) >= 0;
	cablen_valid	= ioctl (fd, SERIAL_GETCABLEN, &cablen) >= 0;
	rloop_valid	= ioctl (fd, SERIAL_GETRLOOP, &rloop) >= 0;
	mtu_valid	= ioctl (fd, SERIAL_GETMTU, &mtu) >= 0;
	rqlen_valid	= ioctl (fd, SERIAL_GETRQLEN, &rqlen) >= 0;

	printf ("%s", chan_name);
	if (port_valid)
		switch (port) {
		case 0:	printf (" (rs232)"); break;
		case 1:	printf (" (v35)"); break;
		case 2:	printf (" (rs530)"); break;
		}
	else if (cable_valid)
		switch (cable) {
		case 0:	printf (" (rs232)"); break;
		case 1:	printf (" (v35)"); break;
		case 2:	printf (" (rs530)"); break;
		case 3:	printf (" (x21)"); break;
		case 4:	printf (" (rs485)"); break;
		case 9:	printf (" (no cable)"); break;
		}
	if (debug_valid && debug)
		printf (" debug=%d", debug);
	if (protocol_valid && *protocol)
		printf (" %.8s", protocol);
	else
		printf (" idle");
	if (cablen_valid)
		printf (" cablen=%s", cablen ? "on" : "off");
	if (keepalive_valid)
		printf (" keepalive=%s", keepalive ? "on" : "off");

	if (cfg_valid)
		switch (cfg) {
		case 'a' :	printf (" cfg=A");	break;
		case 'b' :	printf (" cfg=B");	break;
		case 'c' :	printf (" cfg=C");	break;
		case 'd' :	printf (" cfg=D");	break;
		default  :	printf (" cfg=unknown");
		}
	if (dir_valid)
		printf (" dir=%d", dir);

	if (baud_valid) {
		if (baud)
			printf (" %ld", baud);
		else
			printf (" extclock");
	}
	if (mtu_valid)
		printf (" mtu=%d", mtu);

	if (aflag && rqlen_valid)
		printf (" rqlen=%d", rqlen);

	if (clk_valid)
		switch (clk) {
		case E1CLK_INTERNAL:	  printf (" syn=int");     break;
		case E1CLK_RECEIVE:	  printf (" syn=rcv");     break;
		case E1CLK_RECEIVE_CHAN0: printf (" syn=rcv0");    break;
		case E1CLK_RECEIVE_CHAN1: printf (" syn=rcv1");    break;
		case E1CLK_RECEIVE_CHAN2: printf (" syn=rcv2");    break;
		case E1CLK_RECEIVE_CHAN3: printf (" syn=rcv3");    break;
		default:                  printf (" syn=%d", clk); break;
		}

	if (dpll_valid)
		printf (" dpll=%s", dpll ? "on" : "off");
	if (nrzi_valid)
		printf (" nrzi=%s", nrzi ? "on" : "off");
	if (invclk_valid)
		printf (" invclk=%s", invclk ? "on" : "off");
	if (invrclk_valid)
		printf (" invrclk=%s", invrclk ? "on" : "off");
	if (invtclk_valid)
		printf (" invtclk=%s", invtclk ? "on" : "off");
	if (unfram_valid)
		printf (" unfram=%s", unfram ? "on" : "off");
	if (use16_valid)
		printf (" use16=%s", use16 ? "on" : "off");
	if (aflag) {
		if (crc4_valid)
			printf (" crc4=%s", crc4 ? "on" : "off");
		if (higain_valid)
			printf (" higain=%s", higain ? "on" : "off");
		if (monitor_valid)
			printf (" monitor=%s", monitor ? "on" : "off");
		if (phony_valid)
			printf (" phony=%s", phony ? "on" : "off");
		if (scrambler_valid)
			printf (" scrambler=%s", scrambler ? "on" : "off");
		if (loop_valid)
			printf (" loop=%s", loop ? "on" : "off");
		if (rloop_valid)
			printf (" rloop=%s", rloop ? "on" : "off");
		if (ami_valid)
			printf (" ami=%s", ami ? "on" : "off");
	}
	if (timeslots_valid)
		printf (" ts=%s", format_timeslots (timeslots));
	if (subchan_valid)
		printf (" pass=%s", format_timeslots (subchan));
	if (level_valid)
		printf (" (level=-%.1fdB)", level / 10.0);
	printf ("\n");
}

static void
setup_chan (int fd, int argc, char **argv)
{
	int i, mode, loop, nrzi, dpll, invclk, phony, use16, crc4, unfram, ami;
	int higain, clk, keepalive, debug, port, dlci, invrclk, invtclk;
	int monitor, dir, scrambler, rloop, cablen;
	int mode_valid;
	long baud, timeslots, mtu, rqlen;

	for (i=0; i<argc; ++i) {
		if (argv[i][0] >= '0' && argv[i][0] <= '9') {
			baud = strtol (argv[i], 0, 10);
			ioctl (fd, SERIAL_SETBAUD, &baud);
		} else if (strcasecmp ("extclock", argv[i]) == 0) {
			baud = 0;
			ioctl (fd, SERIAL_SETBAUD, &baud);
		} else if (strncasecmp ("cfg=", argv[i], 4) == 0) {
			if (strncasecmp ("a", argv[i]+4, 1) == 0)
				ioctl (fd, SERIAL_SETCFG, "a");
			else if (strncasecmp ("b", argv[i]+4, 1) == 0)
				ioctl (fd, SERIAL_SETCFG, "b");
			else if (strncasecmp ("c", argv[i]+4, 1) == 0)
				ioctl (fd, SERIAL_SETCFG, "c");
			else if (strncasecmp ("d", argv[i]+4, 1) == 0)
				ioctl (fd, SERIAL_SETCFG, "d");
			else {
				fprintf (stderr, "invalid cfg\n");
				exit (-1);
			}
		} else if (strcasecmp ("idle", argv[i]) == 0)
			ioctl (fd, SERIAL_SETPROTO, "\0\0\0\0\0\0\0");
		else if (strcasecmp ("async", argv[i]) == 0) {
			mode = SERIAL_ASYNC;
			if (ioctl (fd, SERIAL_SETMODE, &mode) >= 0)
				ioctl (fd, SERIAL_SETPROTO, "async\0\0");
		} else if (strcasecmp ("sync", argv[i]) == 0) {
			mode = SERIAL_HDLC;
			if (ioctl (fd, SERIAL_SETMODE, &mode) >= 0)
				ioctl (fd, SERIAL_SETPROTO, "sync\0\0\0");
		} else if (strcasecmp ("cisco", argv[i]) == 0) {
			mode = SERIAL_HDLC;
			ioctl (fd, SERIAL_SETMODE, &mode);
			ioctl (fd, SERIAL_SETPROTO, "cisco\0\0");
		} else if (strcasecmp ("rbrg", argv[i]) == 0) {
			mode = SERIAL_HDLC;
			ioctl (fd, SERIAL_SETMODE, &mode);
			ioctl (fd, SERIAL_SETPROTO, "rbrg\0\0\0");
		} else if (strcasecmp ("raw", argv[i]) == 0) {
			mode = SERIAL_HDLC;
			ioctl (fd, SERIAL_SETMODE, &mode);
			ioctl (fd, SERIAL_SETPROTO, "raw\0\0\0\0");
		} else if (strcasecmp ("packet", argv[i]) == 0) {
			mode = SERIAL_HDLC;
			ioctl (fd, SERIAL_SETMODE, &mode);
			ioctl (fd, SERIAL_SETPROTO, "packet\0");
		} else if (strcasecmp ("ppp", argv[i]) == 0) {
			/* check that ppp line discipline is present */
			if (ppp_ok ()) {
				mode = SERIAL_HDLC;
				ioctl (fd, SERIAL_SETMODE, &mode);
				ioctl (fd, SERIAL_SETPROTO, "ppp\0\0\0\0");
			}
		} else if (strncasecmp ("keepalive=", argv[i], 10) == 0) {
			keepalive = (strcasecmp ("on", argv[i] + 10) == 0);
			ioctl (fd, SERIAL_SETKEEPALIVE, &keepalive);
		} else if (strcasecmp ("fr", argv[i]) == 0) {
			mode = SERIAL_HDLC;
			ioctl (fd, SERIAL_SETMODE, &mode);
			ioctl (fd, SERIAL_SETPROTO, "fr\0\0\0\0\0");
		} else if (strcasecmp ("zaptel", argv[i]) == 0) {
			mode = SERIAL_HDLC;
			ioctl (fd, SERIAL_SETMODE, &mode);
			ioctl (fd, SERIAL_SETPROTO, "zaptel\0");
		} else if (strncasecmp ("debug=", argv[i], 6) == 0) {
			debug = strtol (argv[i]+6, 0, 10);
			mode_valid = ioctl (fd, SERIAL_GETMODE, &mode) >= 0;
			if (!mode_valid || mode != SERIAL_ASYNC) {
				if (debug == 0) {
					set_debug_ifconfig(0);
				} else {
					ioctl (fd, SERIAL_SETDEBUG, &debug);
					set_debug_ifconfig(1);
				}
			} else {
				ioctl (fd, SERIAL_SETDEBUG, &debug);
			}
		} else if (strncasecmp ("loop=", argv[i], 5) == 0) {
			loop = (strcasecmp ("on", argv[i] + 5) == 0);
			ioctl (fd, SERIAL_SETLOOP, &loop);
		} else if (strncasecmp ("rloop=", argv[i], 6) == 0) {
			rloop = (strcasecmp ("on", argv[i] + 6) == 0);
			ioctl (fd, SERIAL_SETRLOOP, &rloop);
		} else if (strncasecmp ("dpll=", argv[i], 5) == 0) {
			dpll = (strcasecmp ("on", argv[i] + 5) == 0);
			ioctl (fd, SERIAL_SETDPLL, &dpll);
		} else if (strncasecmp ("nrzi=", argv[i], 5) == 0) {
			nrzi = (strcasecmp ("on", argv[i] + 5) == 0);
			ioctl (fd, SERIAL_SETNRZI, &nrzi);
		} else if (strncasecmp ("invclk=", argv[i], 7) == 0) {
			invclk = (strcasecmp ("on", argv[i] + 7) == 0);
			ioctl (fd, SERIAL_SETINVCLK, &invclk);
		} else if (strncasecmp ("invrclk=", argv[i], 8) == 0) {
			invrclk = (strcasecmp ("on", argv[i] + 8) == 0);
			ioctl (fd, SERIAL_SETINVRCLK, &invrclk);
		} else if (strncasecmp ("invtclk=", argv[i], 8) == 0) {
			invtclk = (strcasecmp ("on", argv[i] + 8) == 0);
			ioctl (fd, SERIAL_SETINVTCLK, &invtclk);
		} else if (strncasecmp ("higain=", argv[i], 7) == 0) {
			higain = (strcasecmp ("on", argv[i] + 7) == 0);
			ioctl (fd, SERIAL_SETHIGAIN, &higain);
		} else if (strncasecmp ("phony=", argv[i], 6) == 0) {
			phony = (strcasecmp ("on", argv[i] + 6) == 0);
			ioctl (fd, SERIAL_SETPHONY, &phony);
		} else if (strncasecmp ("unfram=", argv[i], 7) == 0) {
			unfram = (strcasecmp ("on", argv[i] + 7) == 0);
			ioctl (fd, SERIAL_SETUNFRAM, &unfram);
		} else if (strncasecmp ("scrambler=", argv[i], 10) == 0) {
			scrambler = (strcasecmp ("on", argv[i] + 10) == 0);
			ioctl (fd, SERIAL_SETSCRAMBLER, &scrambler);
		} else if (strncasecmp ("monitor=", argv[i], 8) == 0) {
			monitor = (strcasecmp ("on", argv[i] + 8) == 0);
			ioctl (fd, SERIAL_SETMONITOR, &monitor);
		} else if (strncasecmp ("use16=", argv[i], 6) == 0) {
			use16 = (strcasecmp ("on", argv[i] + 6) == 0);
			ioctl (fd, SERIAL_SETUSE16, &use16);
		} else if (strncasecmp ("crc4=", argv[i], 5) == 0) {
			crc4 = (strcasecmp ("on", argv[i] + 5) == 0);
			ioctl (fd, SERIAL_SETCRC4, &crc4);
		} else if (strncasecmp ("ami=", argv[i], 4) == 0) {
			ami = (strcasecmp ("on", argv[i] + 4) == 0);
			ioctl (fd, SERIAL_SETLCODE, &ami);
		} else if (strncasecmp ("mtu=", argv[i], 4) == 0) {
			mtu = strtol (argv[i] + 4, 0, 10);
			ioctl (fd, SERIAL_SETMTU, &mtu);
		} else if (strncasecmp ("rqlen=", argv[i], 6) == 0) {
			rqlen = strtol (argv[i] + 6, 0, 10);
			ioctl (fd, SERIAL_SETRQLEN, &rqlen);
		} else if (strcasecmp ("syn=int", argv[i]) == 0) {
			clk = E1CLK_INTERNAL;
			ioctl (fd, SERIAL_SETCLK, &clk);
		} else if (strcasecmp ("syn=rcv", argv[i]) == 0) {
			clk = E1CLK_RECEIVE;
			ioctl (fd, SERIAL_SETCLK, &clk);
		} else if (strcasecmp ("syn=rcv0", argv[i]) == 0) {
			clk = E1CLK_RECEIVE_CHAN0;
			ioctl (fd, SERIAL_SETCLK, &clk);
		} else if (strcasecmp ("syn=rcv1", argv[i]) == 0) {
			clk = E1CLK_RECEIVE_CHAN1;
			ioctl (fd, SERIAL_SETCLK, &clk);
		} else if (strcasecmp ("syn=rcv2", argv[i]) == 0) {
			clk = E1CLK_RECEIVE_CHAN2;
			ioctl (fd, SERIAL_SETCLK, &clk);
		} else if (strcasecmp ("syn=rcv3", argv[i]) == 0) {
			clk = E1CLK_RECEIVE_CHAN3;
			ioctl (fd, SERIAL_SETCLK, &clk);
		} else if (strncasecmp ("ts=", argv[i], 3) == 0) {
			timeslots = scan_timeslots (argv[i] + 3);
			ioctl (fd, SERIAL_SETTIMESLOTS, &timeslots);
		} else if (strncasecmp ("pass=", argv[i], 5) == 0) {
			timeslots = scan_timeslots (argv[i] + 5);
			ioctl (fd, SERIAL_SETSUBCHAN, &timeslots);
		} else if (strncasecmp ("dlci", argv[i], 4) == 0) {
			dlci = strtol (argv[i]+4, 0, 10);
			ioctl (fd, SERIAL_ADDDLCI, &dlci);
		} else if (strncasecmp ("dir=", argv[i], 4) == 0) {
			dir = strtol (argv[i]+4, 0, 10);
			ioctl (fd, SERIAL_SETDIR, &dir);
		} else if (strncasecmp ("port=", argv[i], 5) == 0) {
			if (strncasecmp ("rs232", argv[i]+5, 5) == 0) {
				port = 0;
				ioctl (fd, SERIAL_SETPORT, &port);
			} else if (strncasecmp ("v35", argv[i]+5, 3) == 0) {
				port = 1;
				ioctl (fd, SERIAL_SETPORT, &port);
			} else if (strncasecmp ("rs449", argv[i]+5, 5) == 0) {
				port = 2;
				ioctl (fd, SERIAL_SETPORT, &port);
			} else
				fprintf (stderr, "invalid port type\n");
				exit (-1);
#if 1
		} else if (strcasecmp ("reset", argv[i]) == 0) {
			ioctl (fd, SERIAL_RESET, 0);
		} else if (strcasecmp ("hwreset", argv[i]) == 0) {
			ioctl (fd, SERIAL_HARDRESET, 0);
#endif
		} else if (strncasecmp ("cablen=", argv[i], 7) == 0) {
			loop = (strcasecmp ("on", argv[i] + 7) == 0);
			ioctl (fd, SERIAL_SETCABLEN, &cablen);
		}
	}
}

static void
get_mask (void)
{
#ifdef __linux__
	int fd;

	fd = open ("/dev/serial/ctl0", 0);
	if (fd < 0) {
		perror ("/dev/serial/ctl0");
		exit (-1);
	}
	if (ioctl (fd, SERIAL_GETREGISTERED, &mask) < 0) {
		perror ("getting list of channels");
		exit (-1);
	}
	close (fd);
#else
	int fd, fd1, fd2, fd3, i;
	char buf [80];

	for (i=0, fd=-1; i<12 && fd<0; i++) {
		sprintf (buf, "/dev/cx%d", i*4);
		fd = open (buf, 0);
	}

	for (i=0, fd1=-1; i<3 && fd1<0; i++) {
		sprintf (buf, "/dev/ct%d", i*2);
		fd1 = open (buf, 0);
	}

	for (i=0, fd2=-1; i<3 && fd2<0; i++) {
		sprintf (buf, "/dev/cp%d", i*4);
		fd2 = open (buf, 0);
	}

	/* Try only one */
	for (i=0, fd3=-1; i<1 && fd3<0; i++) {
		sprintf (buf, "/dev/ce%d", i*4);
		fd3 = open (buf, 0);
	}

	if ((fd < 0) && (fd1 < 0) && (fd2 < 0) && (fd3 < 0)) {
		fprintf (stderr, "No Cronyx adapters installed\n");
		exit (-1);
	}

	if (fd >= 0) {
		if (ioctl (fd, SERIAL_GETREGISTERED, &mask) < 0) {
			perror ("getting list of channels");
			exit (-1);
		}
		close (fd);
	}

	if (fd1 >= 0) {
		if (ioctl (fd1, SERIAL_GETREGISTERED, (mask+16)) < 0) {
			perror ("getting list of channels");
			exit (-1);
		}
		close (fd1);
	}

	if (fd2 >= 0) {
		if (ioctl (fd2, SERIAL_GETREGISTERED, (mask+32)) < 0) {
			perror ("getting list of channels");
			exit (-1);
		}
		close (fd2);
	}

	if (fd3 >= 0) {
		if (ioctl (fd3, SERIAL_GETREGISTERED, (mask+48)) < 0) {
			perror ("getting list of channels");
			exit (-1);
		}
		close (fd3);
	}
#endif
}

static int
open_chan_ctl (int num)
{
	char device [80];
	int fd;

#ifdef __linux__
	sprintf (device, "/dev/serial/ctl%d", num);
#else
	switch (adapter_type) {
	case 0:
		sprintf (device, "/dev/cx%d", num);
		break;
	case 1:
		sprintf (device, "/dev/ct%d", num);
		break;
	case 2:
		sprintf (device, "/dev/cp%d", num);
		break;
	case 3:
		sprintf (device, "/dev/ce%d", num);
		break;
	}
#endif
	fd = open (device, 0);
	if (fd < 0) {
		if (errno == ENODEV)
			fprintf (stderr, "chan%d: not configured\n", num);
		else
			perror (device);
		exit (-1);
	}
#ifdef __linux__
	if (ioctl (fd, SERIAL_GETNAME, &chan_name) < 0)
		sprintf (chan_name, "chan%d", num);
#else
	switch (adapter_type) {
	case 0: sprintf (chan_name, "cx%d", num); break;
	case 1: sprintf (chan_name, "ct%d", num); break;
	case 2: sprintf (chan_name, "cp%d", num); break;
	case 3: sprintf (chan_name, "ce%d", num); break;
	}
#endif
	return fd;
}

int
main (int argc, char **argv)
{
	char *p;
	int fd, need_header, chan_num;

	if (argc > 1 && strcmp(argv[1], "help") == 0)
		usage();

	for (;;) {
		switch (getopt (argc, argv, "mseftucviax")) {
		case -1:
			break;
		case 'a':
			++aflag;
			continue;
		case 'm':
			++mflag;
			continue;
		case 's':
			++sflag;
			continue;
		case 'e':
			++eflag;
			continue;
		case 'f':
			++eflag;
			++fflag;
			continue;
		case 't':
			++tflag;
			continue;
		case 'u':
			++tflag;
			++uflag;
			continue;
		case 'c':
			++cflag;
			continue;
		case 'v':
			++vflag;
			continue;
		case 'i':
			++iflag;
			continue;
		case 'x':
			++xflag;
			continue;
		default:
			usage();
		}
		break;
	}
	argc -= optind;
	argv += optind;

	if (argc <= 0) {
		get_mask ();
		need_header = 1;
		adapter_type = 0;
#ifndef __linux__
		for (; adapter_type < 4; ++adapter_type)
#endif
		{
		for (chan_num=0; chan_num<MAXCHAN; ++chan_num)
			if (mask[adapter_type*16+chan_num/8] & 1 << (chan_num & 7)) {
				fd = open_chan_ctl (chan_num);
				if (vflag) {
#ifdef __linux__
				char buf[256];
				if (ioctl (fd, SERIAL_GETVERSIONSTRING, &buf) >= 0) {
					printf ("Version: %s\n", buf);
					close (fd);
					return (0);
				}
#endif
				}
				if (iflag) {
					print_chan (fd);
					print_ifconfig (fd);
				} else if (sflag||xflag)
					print_stats (fd, need_header);
				else if (mflag)
					print_modems (fd, need_header);
				else if (eflag)
					print_e1_stats (fd, need_header);
				else if (tflag)
					print_e3_stats (fd, need_header);
				else if (cflag)
					clear_stats (fd);
				else
					print_chan (fd);
				close (fd);
				need_header = 0;
			}
		}
		return (0);
	}

	p = argv[0] + strlen (argv[0]);
	while (p > argv[0] && p[-1] >= '0' && p[-1] <= '9')
		--p;
	chan_num = strtol (p, 0, 10);
#ifndef __linux__
	if (strncasecmp ("cx", argv[0], 2)==0)
		adapter_type = 0;
	else if (strncasecmp ("ct", argv[0], 2)==0)
		adapter_type = 1;
	else if (strncasecmp ("cp", argv[0], 2)==0)
		adapter_type = 2;
	else if (strncasecmp ("ce", argv[0], 2)==0)
		adapter_type = 3;
	else {
		fprintf (stderr, "Wrong channel name\n");
		exit (-1);
	}
#endif
	argc--;
	argv++;

	fd = open_chan_ctl (chan_num);
	if (vflag) {
#ifdef __linux__
		char buf[256];
		if (ioctl (fd, SERIAL_GETVERSIONSTRING, &buf) >= 0)
			printf ("Version: %s\n", buf);
#endif
	}
	if (iflag) {
		print_chan (fd);
		print_ifconfig (fd);
		close (fd);
		return (0);
	}
	if (sflag||xflag) {
		print_stats (fd, 1);
		close (fd);
		return (0);
	}
	if (mflag) {
		print_modems (fd, 1);
		close (fd);
		return (0);
	}
	if (eflag) {
		print_e1_stats (fd, 1);
		close (fd);
		return (0);
	}
	if (tflag) {
		print_e3_stats (fd, 1);
		close (fd);
		return (0);
	}
	if (cflag) {
		clear_stats (fd);
		close (fd);
		return (0);
	}
	if (argc > 0)
		setup_chan (fd, argc, argv);
	else
		print_chan (fd);
	close (fd);
	return (0);
}
