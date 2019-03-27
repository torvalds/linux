/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * btsockstat.c
 *
 * Copyright (c) 2001-2002 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: btsockstat.c,v 1.8 2003/05/21 22:40:25 max Exp $
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/callout.h>
#include <sys/param.h>
#include <sys/protosw.h>
#include <sys/queue.h>
#include <sys/socket.h>
#define	_WANT_SOCKET
#include <sys/socketvar.h>

#include <net/if.h>

#define L2CAP_SOCKET_CHECKED
#include <bluetooth.h>
#include <err.h>
#include <fcntl.h>
#include <kvm.h>
#include <limits.h>
#include <nlist.h>

#include <netgraph/bluetooth/include/ng_bluetooth.h>
#include <netgraph/bluetooth/include/ng_btsocket_hci_raw.h>
#include <netgraph/bluetooth/include/ng_btsocket_l2cap.h>
#include <netgraph/bluetooth/include/ng_btsocket_rfcomm.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void	hcirawpr   (kvm_t *kvmd, u_long addr);
static void	l2caprawpr (kvm_t *kvmd, u_long addr);
static void	l2cappr    (kvm_t *kvmd, u_long addr);
static void	l2caprtpr  (kvm_t *kvmd, u_long addr);
static void	rfcommpr   (kvm_t *kvmd, u_long addr);
static void	rfcommpr_s (kvm_t *kvmd, u_long addr);

static char *	bdaddrpr   (bdaddr_p const ba, char *str, int len);

static kvm_t *	kopen      (char const *memf);
static int	kread      (kvm_t *kvmd, u_long addr, char *buffer, int size);

static void	usage      (void);

/*
 * List of symbols
 */

static struct nlist	nl[] = {
#define N_HCI_RAW	0
	{ "_ng_btsocket_hci_raw_sockets" },
#define N_L2CAP_RAW	1
	{ "_ng_btsocket_l2cap_raw_sockets" },
#define N_L2CAP		2
	{ "_ng_btsocket_l2cap_sockets" },
#define N_L2CAP_RAW_RT	3
	{ "_ng_btsocket_l2cap_raw_rt" },
#define N_L2CAP_RT	4
	{ "_ng_btsocket_l2cap_rt" },
#define N_RFCOMM	5
	{ "_ng_btsocket_rfcomm_sockets" },
#define N_RFCOMM_S	6
	{ "_ng_btsocket_rfcomm_sessions" },
	{ "" },
};

#define state2str(x) \
	(((x) >= sizeof(states)/sizeof(states[0]))? "UNKNOWN" : states[(x)])

/*
 * Main
 */

static int	numeric_bdaddr = 0;

int
main(int argc, char *argv[])
{
	int	 opt, proto = -1, route = 0;
	kvm_t	*kvmd = NULL;
	char	*memf = NULL;

	while ((opt = getopt(argc, argv, "hnM:p:r")) != -1) {
		switch (opt) {
		case 'n':
			numeric_bdaddr = 1;
			break;

		case 'M':
			memf = optarg;
			break;

		case 'p':
			if (strcasecmp(optarg, "hci_raw") == 0)
				proto = N_HCI_RAW;
			else if (strcasecmp(optarg, "l2cap_raw") == 0)
				proto = N_L2CAP_RAW;
			else if (strcasecmp(optarg, "l2cap") == 0)
				proto = N_L2CAP;
			else if (strcasecmp(optarg, "rfcomm") == 0)
				proto = N_RFCOMM;
			else if (strcasecmp(optarg, "rfcomm_s") == 0)
				proto = N_RFCOMM_S; 
			else
				usage();
				/* NOT REACHED */
			break;

		case 'r':
			route = 1;
			break;

		case 'h':
		default:
			usage();
			/* NOT REACHED */
		}
	}

	if ((proto == N_HCI_RAW || proto == N_RFCOMM || proto == N_RFCOMM_S) && route)
		usage();
		/* NOT REACHED */

	/*
	 * Discard setgid privileges if not the running kernel so that
	 * bad guys can't print interesting stuff from kernel memory.
	 */
	if (memf != NULL)
		if (setgid(getgid()) != 0)
			err(1, "setgid");

	kvmd = kopen(memf);
	if (kvmd == NULL)
		return (1);

	switch (proto) {
	case N_HCI_RAW:
		hcirawpr(kvmd, nl[N_HCI_RAW].n_value);
		break;

	case N_L2CAP_RAW:
		if (route)
			l2caprtpr(kvmd, nl[N_L2CAP_RAW_RT].n_value);
		else
			l2caprawpr(kvmd, nl[N_L2CAP_RAW].n_value);
		break;

	case N_L2CAP:
		if (route) 
			l2caprtpr(kvmd, nl[N_L2CAP_RT].n_value);
		else
			l2cappr(kvmd, nl[N_L2CAP].n_value);
		break;

	case N_RFCOMM:
		rfcommpr(kvmd, nl[N_RFCOMM].n_value);
		break;

	case N_RFCOMM_S:
		rfcommpr_s(kvmd, nl[N_RFCOMM_S].n_value);
		break;

	default:
		if (route) {
			l2caprtpr(kvmd, nl[N_L2CAP_RAW_RT].n_value);
			l2caprtpr(kvmd, nl[N_L2CAP_RT].n_value);
		} else {
			hcirawpr(kvmd, nl[N_HCI_RAW].n_value);
			l2caprawpr(kvmd, nl[N_L2CAP_RAW].n_value);
			l2cappr(kvmd, nl[N_L2CAP].n_value);
			rfcommpr_s(kvmd, nl[N_RFCOMM_S].n_value);
			rfcommpr(kvmd, nl[N_RFCOMM].n_value);
		}
		break;
	}

	return (kvm_close(kvmd));
} /* main */

/*
 * Print raw HCI sockets
 */

static void
hcirawpr(kvm_t *kvmd, u_long addr)
{
	ng_btsocket_hci_raw_pcb_p	this = NULL, next = NULL;
	ng_btsocket_hci_raw_pcb_t	pcb;
	struct socket			so;
	int				first = 1;

	if (addr == 0)
		return;

        if (kread(kvmd, addr, (char *) &this, sizeof(this)) < 0)
		return;

	for ( ; this != NULL; this = next) {
		if (kread(kvmd, (u_long) this, (char *) &pcb, sizeof(pcb)) < 0)
			return;
		if (kread(kvmd, (u_long) pcb.so, (char *) &so, sizeof(so)) < 0)
			return;

		next = LIST_NEXT(&pcb, next);

		if (first) {
			first = 0;
			fprintf(stdout,
"Active raw HCI sockets\n" \
"%-8.8s %-8.8s %-6.6s %-6.6s %-6.6s %-16.16s\n",
				"Socket",
				"PCB",
				"Flags",
				"Recv-Q",
				"Send-Q",
				"Local address");
		}

		if (pcb.addr.hci_node[0] == 0) {
			pcb.addr.hci_node[0] = '*';
			pcb.addr.hci_node[1] = 0;
		}

		fprintf(stdout,
"%-8lx %-8lx %-6.6x %6d %6d %-16.16s\n",
			(unsigned long) pcb.so,
			(unsigned long) this,
			pcb.flags,
			so.so_rcv.sb_ccc,
			so.so_snd.sb_ccc,
			pcb.addr.hci_node);
	}
} /* hcirawpr */

/*
 * Print raw L2CAP sockets
 */

static void
l2caprawpr(kvm_t *kvmd, u_long addr)
{
	ng_btsocket_l2cap_raw_pcb_p	this = NULL, next = NULL;
	ng_btsocket_l2cap_raw_pcb_t	pcb;
	struct socket			so;
	int				first = 1;

	if (addr == 0)
		return;

        if (kread(kvmd, addr, (char *) &this, sizeof(this)) < 0)
		return;

	for ( ; this != NULL; this = next) {
		if (kread(kvmd, (u_long) this, (char *) &pcb, sizeof(pcb)) < 0)
			return;
		if (kread(kvmd, (u_long) pcb.so, (char *) &so, sizeof(so)) < 0)
			return;

		next = LIST_NEXT(&pcb, next);

		if (first) {
			first = 0;
			fprintf(stdout, 
"Active raw L2CAP sockets\n" \
"%-8.8s %-8.8s %-6.6s %-6.6s %-17.17s\n",
				"Socket",
				"PCB",
				"Recv-Q",
				"Send-Q",
				"Local address");
		}

		fprintf(stdout,
"%-8lx %-8lx %6d %6d %-17.17s\n",
			(unsigned long) pcb.so,
			(unsigned long) this,
			so.so_rcv.sb_ccc,
			so.so_snd.sb_ccc,
			bdaddrpr(&pcb.src, NULL, 0));
	}
} /* l2caprawpr */

/*
 * Print L2CAP sockets
 */

static void
l2cappr(kvm_t *kvmd, u_long addr)
{
	static char const * const	states[] = {
	/* NG_BTSOCKET_L2CAP_CLOSED */		"CLOSED",
	/* NG_BTSOCKET_L2CAP_CONNECTING */	"CON",
	/* NG_BTSOCKET_L2CAP_CONFIGURING */	"CONFIG",
	/* NG_BTSOCKET_L2CAP_OPEN */		"OPEN",
	/* NG_BTSOCKET_L2CAP_DISCONNECTING */	"DISCON"
	};

	ng_btsocket_l2cap_pcb_p	this = NULL, next = NULL;
	ng_btsocket_l2cap_pcb_t	pcb;
	struct socket		so;
	int			first = 1;
	char			local[24], remote[24];

	if (addr == 0)
		return;

        if (kread(kvmd, addr, (char *) &this, sizeof(this)) < 0)
		return;

	for ( ; this != NULL; this = next) {
		if (kread(kvmd, (u_long) this, (char *) &pcb, sizeof(pcb)) < 0)
			return;
		if (kread(kvmd, (u_long) pcb.so, (char *) &so, sizeof(so)) < 0)
			return;

		next = LIST_NEXT(&pcb, next);

		if (first) {
			first = 0;
			fprintf(stdout,
"Active L2CAP sockets\n" \
"%-8.8s %-6.6s %-6.6s %-23.23s %-17.17s %-5.5s %s\n",
				"PCB",
				"Recv-Q",
				"Send-Q",
				"Local address/PSM",
				"Foreign address",
				"CID",
				"State");
		}

		fprintf(stdout,
"%-8lx %6d %6d %-17.17s/%-5d %-17.17s %-5d %s\n",
			(unsigned long) this,
			so.so_rcv.sb_ccc,
			so.so_snd.sb_ccc,
			bdaddrpr(&pcb.src, local, sizeof(local)),
			pcb.psm,
			bdaddrpr(&pcb.dst, remote, sizeof(remote)),
			pcb.cid,
			(so.so_options & SO_ACCEPTCONN)?
				"LISTEN" : state2str(pcb.state));
	}
} /* l2cappr */

/*
 * Print L2CAP routing table
 */

static void
l2caprtpr(kvm_t *kvmd, u_long addr)
{
	ng_btsocket_l2cap_rtentry_p	this = NULL, next = NULL;
	ng_btsocket_l2cap_rtentry_t	rt;
	int				first = 1;

	if (addr == 0)
		return;

	if (kread(kvmd, addr, (char *) &this, sizeof(this)) < 0)
		return;

	for ( ; this != NULL; this = next) {
		if (kread(kvmd, (u_long) this, (char *) &rt, sizeof(rt)) < 0)
			return;

		next = LIST_NEXT(&rt, next);

		if (first) {
			first = 0;
			fprintf(stdout,
"Known %sL2CAP routes\n", (addr == nl[N_L2CAP_RAW_RT].n_value)?  "raw " : "");
			fprintf(stdout,
"%-8.8s %-8.8s %-17.17s\n",	"RTentry",
				"Hook",
				"BD_ADDR");
		}

		fprintf(stdout,
"%-8lx %-8lx %-17.17s\n",
			(unsigned long) this,
			(unsigned long) rt.hook,
			bdaddrpr(&rt.src, NULL, 0));
	}
} /* l2caprtpr */

/*
 * Print RFCOMM sockets
 */

static void
rfcommpr(kvm_t *kvmd, u_long addr)
{
	static char const * const	states[] = {
	/* NG_BTSOCKET_RFCOMM_DLC_CLOSED */	   "CLOSED",
	/* NG_BTSOCKET_RFCOMM_DLC_W4_CONNECT */	   "W4CON",
	/* NG_BTSOCKET_RFCOMM_DLC_CONFIGURING */   "CONFIG",
	/* NG_BTSOCKET_RFCOMM_DLC_CONNECTING */    "CONN",
	/* NG_BTSOCKET_RFCOMM_DLC_CONNECTED */     "OPEN",
	/* NG_BTSOCKET_RFCOMM_DLC_DISCONNECTING */ "DISCON"
	};

	ng_btsocket_rfcomm_pcb_p	this = NULL, next = NULL;
	ng_btsocket_rfcomm_pcb_t	pcb;
	struct socket			so;
	int				first = 1;
	char				local[24], remote[24];

	if (addr == 0)
		return;

        if (kread(kvmd, addr, (char *) &this, sizeof(this)) < 0)
		return;

	for ( ; this != NULL; this = next) {
		if (kread(kvmd, (u_long) this, (char *) &pcb, sizeof(pcb)) < 0)
			return;
		if (kread(kvmd, (u_long) pcb.so, (char *) &so, sizeof(so)) < 0)
			return;

		next = LIST_NEXT(&pcb, next);

		if (first) {
			first = 0;
			fprintf(stdout,
"Active RFCOMM sockets\n" \
"%-8.8s %-6.6s %-6.6s %-17.17s %-17.17s %-4.4s %-4.4s %s\n",
				"PCB",
				"Recv-Q",
				"Send-Q",
				"Local address",
				"Foreign address",
				"Chan",
				"DLCI",
				"State");
		}

		fprintf(stdout,
"%-8lx %6d %6d %-17.17s %-17.17s %-4d %-4d %s\n",
			(unsigned long) this,
			so.so_rcv.sb_ccc,
			so.so_snd.sb_ccc,
			bdaddrpr(&pcb.src, local, sizeof(local)),
			bdaddrpr(&pcb.dst, remote, sizeof(remote)),
			pcb.channel,
			pcb.dlci,
			(so.so_options & SO_ACCEPTCONN)?
				"LISTEN" : state2str(pcb.state));
	}
} /* rfcommpr */

/*
 * Print RFCOMM sessions
 */

static void
rfcommpr_s(kvm_t *kvmd, u_long addr)
{
	static char const * const	states[] = {
	/* NG_BTSOCKET_RFCOMM_SESSION_CLOSED */	       "CLOSED",
	/* NG_BTSOCKET_RFCOMM_SESSION_LISTENING */     "LISTEN",
	/* NG_BTSOCKET_RFCOMM_SESSION_CONNECTING */    "CONNECTING",
	/* NG_BTSOCKET_RFCOMM_SESSION_CONNECTED */     "CONNECTED",
	/* NG_BTSOCKET_RFCOMM_SESSION_OPEN */          "OPEN",
	/* NG_BTSOCKET_RFCOMM_SESSION_DISCONNECTING */ "DISCONNECTING"
	};

	ng_btsocket_rfcomm_session_p	this = NULL, next = NULL;
	ng_btsocket_rfcomm_session_t	s;
	struct socket			so;
	int				first = 1;

	if (addr == 0)
		return;

        if (kread(kvmd, addr, (char *) &this, sizeof(this)) < 0)
		return;

	for ( ; this != NULL; this = next) {
		if (kread(kvmd, (u_long) this, (char *) &s, sizeof(s)) < 0)
			return;
		if (kread(kvmd, (u_long) s.l2so, (char *) &so, sizeof(so)) < 0)
			return;

		next = LIST_NEXT(&s, next);

		if (first) {
			first = 0;
			fprintf(stdout,
"Active RFCOMM sessions\n" \
"%-8.8s %-8.8s %-4.4s %-5.5s %-5.5s %-4.4s %s\n",
				"L2PCB",
				"PCB",
				"Flags",
				"MTU",
				"Out-Q",
				"DLCs",
				"State");
		}

		fprintf(stdout,
"%-8lx %-8lx %-4x %-5d %-5d %-4s %s\n",
			(unsigned long) so.so_pcb,
			(unsigned long) this,
			s.flags,
			s.mtu,
			s.outq.len,
			LIST_EMPTY(&s.dlcs)? "No" : "Yes",
			state2str(s.state));
	}
} /* rfcommpr_s */

/*
 * Return BD_ADDR as string
 */

static char *
bdaddrpr(bdaddr_p const ba, char *str, int len)
{
	static char	 buffer[MAXHOSTNAMELEN];
	struct hostent	*he = NULL;

	if (str == NULL) {
		str = buffer;
		len = sizeof(buffer);
	}

	if (memcmp(ba, NG_HCI_BDADDR_ANY, sizeof(*ba)) == 0) {
		str[0] = '*';
		str[1] = 0;

		return (str);
	}

	if (!numeric_bdaddr &&
	    (he = bt_gethostbyaddr((char *)ba, sizeof(*ba), AF_BLUETOOTH)) != NULL) {
		strlcpy(str, he->h_name, len);

		return (str);
	}

	bt_ntoa(ba, str);

	return (str);
} /* bdaddrpr */

/*
 * Open kvm
 */

static kvm_t *
kopen(char const *memf)
{
	kvm_t	*kvmd = NULL;
	char	 errbuf[_POSIX2_LINE_MAX];

	kvmd = kvm_openfiles(NULL, memf, NULL, O_RDONLY, errbuf);
	if (setgid(getgid()) != 0)
		err(1, "setgid");
	if (kvmd == NULL) {
		warnx("kvm_openfiles: %s", errbuf);
		return (NULL);
	}

	if (kvm_nlist(kvmd, nl) < 0) {
		warnx("kvm_nlist: %s", kvm_geterr(kvmd));
		goto fail;
	}

	if (nl[0].n_type == 0) {
		warnx("kvm_nlist: no namelist");
		goto fail;
	}

	return (kvmd);
fail:
	kvm_close(kvmd);

	return (NULL);
} /* kopen */

/*
 * Read kvm
 */

static int
kread(kvm_t *kvmd, u_long addr, char *buffer, int size)
{
	if (kvmd == NULL || buffer == NULL)
		return (-1);

	if (kvm_read(kvmd, addr, buffer, size) != size) {
		warnx("kvm_read: %s", kvm_geterr(kvmd));
		return (-1);
	}

	return (0);
} /* kread */

/*
 * Print usage and exit
 */

static void
usage(void)
{
	fprintf(stdout, "Usage: btsockstat [-M core ] [-n] [-p proto] [-r]\n");
	exit(255);
} /* usage */

