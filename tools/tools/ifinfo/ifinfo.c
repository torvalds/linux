/*
 * Copyright 1996 Massachusetts Institute of Technology
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby
 * granted, provided that both the above copyright notice and this
 * permission notice appear in all copies, that both the above
 * copyright notice and this permission notice appear in all
 * supporting documentation, and that the name of M.I.T. not be used
 * in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.  M.I.T. makes
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied
 * warranty.
 * 
 * THIS SOFTWARE IS PROVIDED BY M.I.T. ``AS IS''.  M.I.T. DISCLAIMS
 * ALL EXPRESS OR IMPLIED WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT
 * SHALL M.I.T. BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#include <sys/types.h>
#include <sys/socket.h>		/* for PF_LINK */
#include <sys/sysctl.h>
#include <sys/time.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <net/if.h>
#include <net/if_types.h>
#include <net/if_mib.h>

#include "ifinfo.h"

static void printit(const struct ifmibdata *, const char *);
static const char *iftype(int);
static const char *ifphys(int, int);
static int isit(int, char **, const char *);
static printfcn findlink(int);

static void
usage(const char *argv0)
{
	fprintf(stderr, "%s: usage:\n\t%s [-l]\n", argv0, argv0);
	exit(EX_USAGE);
}

int
main(int argc, char **argv)
{
	int i, maxifno, retval;
	struct ifmibdata ifmd;
	int name[6];
	size_t len;
	int c;
	int dolink = 0;
	void *linkmib;
	size_t linkmiblen;
	printfcn pf;
	char *dname;

	while ((c = getopt(argc, argv, "l")) != -1) {
		switch(c) {
		case 'l':
			dolink = 1;
			break;
		default:
			usage(argv[0]);
		}
	}
	
	retval = 1;

	name[0] = CTL_NET;
	name[1] = PF_LINK;
	name[2] = NETLINK_GENERIC;
	name[3] = IFMIB_SYSTEM;
	name[4] = IFMIB_IFCOUNT;

	len = sizeof maxifno;
	if (sysctl(name, 5, &maxifno, &len, 0, 0) < 0)
		err(EX_OSERR, "sysctl(net.link.generic.system.ifcount)");

	for (i = 1; i <= maxifno; i++) {
		len = sizeof ifmd;
		name[3] = IFMIB_IFDATA;
		name[4] = i;
		name[5] = IFDATA_GENERAL;
		if (sysctl(name, 6, &ifmd, &len, 0, 0) < 0) {
			if (errno == ENOENT)
				continue;

			err(EX_OSERR, "sysctl(net.link.ifdata.%d.general)",
			    i);
		}

		if (!isit(argc - optind, argv + optind, ifmd.ifmd_name))
			continue;

		dname = NULL;
		len = 0;
		name[5] = IFDATA_DRIVERNAME;
		if (sysctl(name, 6, NULL, &len, 0, 0) < 0) {
			warn("sysctl(net.link.ifdata.%d.drivername)", i);
		} else {
			if ((dname = malloc(len)) == NULL)
				err(EX_OSERR, NULL);
			if (sysctl(name, 6, dname, &len, 0, 0) < 0) {
				warn("sysctl(net.link.ifdata.%d.drivername)",
				    i);
				free(dname);
				dname = NULL;
			}
		}
		printit(&ifmd, dname);
		free(dname);
		if (dolink && (pf = findlink(ifmd.ifmd_data.ifi_type))) {
			name[5] = IFDATA_LINKSPECIFIC;
			if (sysctl(name, 6, 0, &linkmiblen, 0, 0) < 0)
				err(EX_OSERR, 
				    "sysctl(net.link.ifdata.%d.linkspec) size",
				    i);
			linkmib = malloc(linkmiblen);
			if (!linkmib)
				err(EX_OSERR, "malloc(%lu)", 
				    (u_long)linkmiblen);
			if (sysctl(name, 6, linkmib, &linkmiblen, 0, 0) < 0)
				err(EX_OSERR, 
				    "sysctl(net.link.ifdata.%d.linkspec)",
				    i);
			pf(linkmib, linkmiblen);
			free(linkmib);
		}
		retval = 0;
	}

	return retval;
}

static void
printit(const struct ifmibdata *ifmd, const char *dname)
{
	printf("Interface %.*s", IFNAMSIZ, ifmd->ifmd_name);
	if (dname != NULL)
		printf(" (%s)", dname);
	printf(":\n");
	printf("\tflags: %x\n", ifmd->ifmd_flags);
	printf("\tpromiscuous listeners: %d\n", ifmd->ifmd_pcount);
	printf("\tsend queue length: %d\n", ifmd->ifmd_snd_len);
	printf("\tsend queue max length: %d\n", ifmd->ifmd_snd_maxlen);
	printf("\tsend queue drops: %d\n", ifmd->ifmd_snd_drops);
	printf("\ttype: %s\n", iftype(ifmd->ifmd_data.ifi_type));
	printf("\tphysical: %s\n", ifphys(ifmd->ifmd_data.ifi_type,
					  ifmd->ifmd_data.ifi_physical));
	printf("\taddress length: %d\n", ifmd->ifmd_data.ifi_addrlen);
	printf("\theader length: %d\n", ifmd->ifmd_data.ifi_hdrlen);
	printf("\tlink state: %u\n", ifmd->ifmd_data.ifi_link_state);
	printf("\tvhid: %u\n", ifmd->ifmd_data.ifi_vhid);
	printf("\tdatalen: %u\n", ifmd->ifmd_data.ifi_datalen);
	printf("\tmtu: %lu\n", ifmd->ifmd_data.ifi_mtu);
	printf("\tmetric: %lu\n", ifmd->ifmd_data.ifi_metric);
	printf("\tline rate: %lu bit/s\n", ifmd->ifmd_data.ifi_baudrate);
	printf("\tpackets received: %lu\n", ifmd->ifmd_data.ifi_ipackets);
	printf("\tinput errors: %lu\n", ifmd->ifmd_data.ifi_ierrors);
	printf("\tpackets transmitted: %lu\n", ifmd->ifmd_data.ifi_opackets);
	printf("\toutput errors: %lu\n", ifmd->ifmd_data.ifi_oerrors);
	printf("\tcollisions: %lu\n", ifmd->ifmd_data.ifi_collisions);
	printf("\tbytes received: %lu\n", ifmd->ifmd_data.ifi_ibytes);
	printf("\tbytes transmitted: %lu\n", ifmd->ifmd_data.ifi_obytes);
	printf("\tmulticasts received: %lu\n", ifmd->ifmd_data.ifi_imcasts);
	printf("\tmulticasts transmitted: %lu\n", ifmd->ifmd_data.ifi_omcasts);
	printf("\tinput queue drops: %lu\n", ifmd->ifmd_data.ifi_iqdrops);
	printf("\tpackets for unknown protocol: %lu\n", 
	       ifmd->ifmd_data.ifi_noproto);
	printf("\tHW offload capabilities: 0x%lx\n",
	    ifmd->ifmd_data.ifi_hwassist);
	printf("\tuptime at attach or stat reset: %lu\n",
	    ifmd->ifmd_data.ifi_epoch);
#ifdef notdef
	printf("\treceive timing: %lu usec\n", ifmd->ifmd_data.ifi_recvtiming);
	printf("\ttransmit timing: %lu usec\n", 
	       ifmd->ifmd_data.ifi_xmittiming);
#endif
}

static const char *const if_types[] = {
	"reserved",
	"other",
	"BBN 1822",
	"HDH 1822",
	"X.25 DDN",
	"X.25",
	"Ethernet",
	"ISO 8802-3 CSMA/CD",
	"ISO 8802-4 Token Bus",
	"ISO 8802-5 Token Ring",
	"ISO 8802-6 DQDB MAN",
	"StarLAN",
	"Proteon proNET-10",
	"Proteon proNET-80",
	"HyperChannel",
	"FDDI",
	"LAP-B",
	"SDLC",
	"T-1",
	"CEPT",
	"Basic rate ISDN",
	"Primary rate ISDN",
	"Proprietary P2P",
	"PPP",
	"Loopback",
	"ISO CLNP over IP",
	"Experimental Ethernet",
	"XNS over IP",
	"SLIP",
	"Ultra Technologies",
	"DS-3",
	"SMDS",
	"Frame Relay",
	"RS-232 serial",
	"Parallel printer port",
	"ARCNET",
	"ARCNET+",
	"ATM",
	"MIOX25",
	"SONET/SDH",
	"X25PLE",
	"ISO 8802-2 LLC",
	"LocalTalk",
	"SMDSDXI",
	"Frame Relay DCE",
	"V.35",
	"HSSI",
	"HIPPI",
	"Generic Modem",
	"ATM AAL5",
	"SONETPATH",
	"SONETVT",
	"SMDS InterCarrier Interface",
	"Proprietary virtual interface",
	"Proprietary multiplexing",
	"Generic tunnel interface",
	"IPv6-to-IPv4 TCP relay capturing interface",
	"6to4 tunnel interface"
};
#define	NIFTYPES ((sizeof if_types)/(sizeof if_types[0]))

static const char *
iftype(int type)
{
	static char buf[256];

	if (type <= 0 || type >= NIFTYPES) {
		sprintf(buf, "unknown type %d", type);
		return buf;
	}

	return if_types[type];
}

static const char *
ifphys(int type, int phys)
{
	static char buf[256];

	sprintf(buf, "unknown physical %d", phys);
	return buf;
}

static int
isit(int argc, char **argv, const char *name)
{
	if (argc == 0)
		return 1;
	for (argc = 0; argv[argc]; argc++) {
		if (strncmp(argv[argc], name, IFNAMSIZ) == 0)
			return 1;
	}
	return 0;
}

static printfcn
findlink(int type)
{
	switch(type) {
	case IFT_ETHER:
	case IFT_ISO88023:
	case IFT_STARLAN:
		return print_1650;
	}

	return 0;
}
