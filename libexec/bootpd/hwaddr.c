/*
 * hwaddr.c - routines that deal with hardware addresses.
 * (i.e. Ethernet)
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#if defined(SUNOS) || defined(SVR4)
#include <sys/sockio.h>
#endif
#ifdef	SVR4
#include <sys/stream.h>
#include <stropts.h>
#include <fcntl.h>
#endif

#ifdef _AIX32
#include <sys/time.h>	/* for struct timeval in net/if.h */
#include <net/if.h> 	/* for struct ifnet in net/if_arp.h */
#endif

#include <net/if_arp.h>
#include <netinet/in.h>

#ifdef WIN_TCP
#include <netinet/if_ether.h>
#include <sys/dlpi.h>
#endif

#include <stdio.h>
#ifndef	NO_UNISTD
#include <unistd.h>
#endif
#include <syslog.h>

#ifndef USE_BFUNCS
/* Yes, memcpy is OK here (no overlapped copies). */
#include <memory.h>
#define bcopy(a,b,c)    memcpy(b,a,c)
#define bzero(p,l)      memset(p,0,l)
#define bcmp(a,b,c)     memcmp(a,b,c)
#endif

#ifndef	ATF_INUSE	/* Not defined on some systems (i.e. Linux) */
#define	ATF_INUSE 0
#endif

/* For BSD 4.4, set arp entry by writing to routing socket */
#if defined(BSD)
#if BSD >= 199306
extern int bsd_arp_set(struct in_addr *, char *, int);
#endif
#endif

#include "bptypes.h"
#include "hwaddr.h"
#include "report.h"

extern int debug;

/*
 * Hardware address lengths (in bytes) and network name based on hardware
 * type code.  List in order specified by Assigned Numbers RFC; Array index
 * is hardware type code.  Entries marked as zero are unknown to the author
 * at this time.  .  .  .
 */

struct hwinfo hwinfolist[] =
{
	{0, "Reserved"},			/* Type 0:  Reserved (don't use this)   */
	{6, "Ethernet"},			/* Type 1:  10Mb Ethernet (48 bits)	*/
	{1, "3Mb Ethernet"},		/* Type 2:   3Mb Ethernet (8 bits)	*/
	{0, "AX.25"},				/* Type 3:  Amateur Radio AX.25		*/
	{1, "ProNET"},				/* Type 4:  Proteon ProNET Token Ring   */
	{0, "Chaos"},				/* Type 5:  Chaos			*/
	{6, "IEEE 802"},			/* Type 6:  IEEE 802 Networks		*/
	{0, "ARCNET"}				/* Type 7:  ARCNET			*/
};
int hwinfocnt = sizeof(hwinfolist) / sizeof(hwinfolist[0]);


/*
 * Setup the arp cache so that IP address 'ia' will be temporarily
 * bound to hardware address 'ha' of length 'len'.
 */
void
setarp(s, ia, hafamily, haddr, halen)
	int s;						/* socket fd */
	struct in_addr *ia;			/* protocol address */
	int hafamily;				/* HW address family */
	u_char *haddr;				/* HW address data */
	int halen;
{
#ifdef	SIOCSARP
#ifdef	WIN_TCP
	/* This is an SVR4 with different networking code from
	 * Wollongong WIN-TCP.  Not quite like the Lachman code.
	 * Code from: drew@drewsun.FEITH.COM (Andrew B. Sudell)
	 */
#undef	SIOCSARP
#define	SIOCSARP ARP_ADD
	struct arptab arpreq;		/* Arp table entry */

	bzero((caddr_t) &arpreq, sizeof(arpreq));
	arpreq.at_flags = ATF_COM;

	/* Set up IP address */
	arpreq.at_in = ia->s_addr;

	/* Set up Hardware Address */
	bcopy(haddr, arpreq.at_enaddr, halen);

	/* Set the Date Link type. */
	/* XXX - Translate (hafamily) to dltype somehow? */
	arpreq.at_dltype = DL_ETHER;

#else	/* WIN_TCP */
	/* Good old Berkeley way. */
	struct arpreq arpreq;		/* Arp request ioctl block */
	struct sockaddr_in *si;
	char *p;

	bzero((caddr_t) &arpreq, sizeof(arpreq));
	arpreq.arp_flags = ATF_INUSE | ATF_COM;

	/* Set up the protocol address. */
	arpreq.arp_pa.sa_family = AF_INET;
	si = (struct sockaddr_in *) &arpreq.arp_pa;
	si->sin_addr = *ia;

	/* Set up the hardware address. */
#ifdef	__linux__	/* XXX - Do others need this? -gwr */
	/*
	 * Linux requires the sa_family field set.
	 * longyear@netcom.com (Al Longyear)
	 */
	arpreq.arp_ha.sa_family = hafamily;
#endif	/* linux */

	/* This variable is just to help catch type mismatches. */
	p = arpreq.arp_ha.sa_data;
	bcopy(haddr, p, halen);
#endif	/* WIN_TCP */

#ifdef	SVR4
	/*
	 * And now the stuff for System V Rel 4.x which does not
	 * appear to allow SIOCxxx ioctls on a socket descriptor.
	 * Thanks to several people: (all sent the same fix)
	 *   Barney Wolff <barney@databus.com>,
	 *   bear@upsys.se (Bj|rn Sj|holm),
	 *   Michael Kuschke <Michael.Kuschke@Materna.DE>,
	 */
	{
		int fd;
		struct strioctl iocb;

		if ((fd=open("/dev/arp", O_RDWR)) < 0) {
			report(LOG_ERR, "open /dev/arp: %s\n", get_errmsg());
		}
		iocb.ic_cmd = SIOCSARP;
		iocb.ic_timout = 0;
		iocb.ic_dp = (char *)&arpreq;
		iocb.ic_len = sizeof(arpreq);
		if (ioctl(fd, I_STR, (caddr_t)&iocb) < 0) {
			report(LOG_ERR, "ioctl I_STR: %s\n", get_errmsg());
		}
		close (fd);
	}
#else	/* SVR4 */
	/*
	 * On SunOS, the ioctl sometimes returns ENXIO, and it
	 * appears to happen when the ARP cache entry you tried
	 * to add is already in the cache.  (Sigh...)
	 * XXX - Should this error simply be ignored? -gwr
	 */
	if (ioctl(s, SIOCSARP, (caddr_t) &arpreq) < 0) {
		report(LOG_ERR, "ioctl SIOCSARP: %s", get_errmsg());
	}
#endif	/* SVR4 */
#else	/* SIOCSARP */
#if defined(BSD) && (BSD >= 199306)
	bsd_arp_set(ia, haddr, halen);
#else
	/*
	 * Oh well, SIOCSARP is not defined.  Just run arp(8).
	 * Need to delete partial entry first on some systems.
	 * XXX - Gag!
	 */
	int status;
	char buf[256];
	char *a;
	extern char *inet_ntoa();

	a = inet_ntoa(*ia);
	snprintf(buf, sizeof(buf), "arp -d %s; arp -s %s %s temp",
			a, a, haddrtoa(haddr, halen));
	if (debug > 2)
		report(LOG_INFO, "%s", buf);
	status = system(buf);
	if (status)
		report(LOG_ERR, "arp failed, exit code=0x%x", status);
	return;
#endif	/* ! 4.4 BSD */
#endif	/* SIOCSARP */
}


/*
 * Convert a hardware address to an ASCII string.
 */
char *
haddrtoa(haddr, hlen)
	u_char *haddr;
	int hlen;
{
	static char haddrbuf[3 * MAXHADDRLEN + 1];
	char *bufptr;

	if (hlen > MAXHADDRLEN)
		hlen = MAXHADDRLEN;

	bufptr = haddrbuf;
	while (hlen > 0) {
		sprintf(bufptr, "%02X:", (unsigned) (*haddr++ & 0xFF));
		bufptr += 3;
		hlen--;
	}
	bufptr[-1] = 0;
	return (haddrbuf);
}


/*
 * haddr_conv802()
 * --------------
 *
 * Converts a backwards address to a canonical address and a canonical address
 * to a backwards address.
 *
 * INPUTS:
 *  adr_in - pointer to six byte string to convert (unsigned char *)
 *  addr_len - how many bytes to convert
 *
 * OUTPUTS:
 *  addr_out - The string is updated to contain the converted address.
 *
 * CALLER:
 *  many
 *
 * DATA:
 *  Uses conv802table to bit-reverse the address bytes.
 */

static u_char conv802table[256] =
{
	/* 0x00 */ 0x00, 0x80, 0x40, 0xC0, 0x20, 0xA0, 0x60, 0xE0,
	/* 0x08 */ 0x10, 0x90, 0x50, 0xD0, 0x30, 0xB0, 0x70, 0xF0,
	/* 0x10 */ 0x08, 0x88, 0x48, 0xC8, 0x28, 0xA8, 0x68, 0xE8,
	/* 0x18 */ 0x18, 0x98, 0x58, 0xD8, 0x38, 0xB8, 0x78, 0xF8,
	/* 0x20 */ 0x04, 0x84, 0x44, 0xC4, 0x24, 0xA4, 0x64, 0xE4,
	/* 0x28 */ 0x14, 0x94, 0x54, 0xD4, 0x34, 0xB4, 0x74, 0xF4,
	/* 0x30 */ 0x0C, 0x8C, 0x4C, 0xCC, 0x2C, 0xAC, 0x6C, 0xEC,
	/* 0x38 */ 0x1C, 0x9C, 0x5C, 0xDC, 0x3C, 0xBC, 0x7C, 0xFC,
	/* 0x40 */ 0x02, 0x82, 0x42, 0xC2, 0x22, 0xA2, 0x62, 0xE2,
	/* 0x48 */ 0x12, 0x92, 0x52, 0xD2, 0x32, 0xB2, 0x72, 0xF2,
	/* 0x50 */ 0x0A, 0x8A, 0x4A, 0xCA, 0x2A, 0xAA, 0x6A, 0xEA,
	/* 0x58 */ 0x1A, 0x9A, 0x5A, 0xDA, 0x3A, 0xBA, 0x7A, 0xFA,
	/* 0x60 */ 0x06, 0x86, 0x46, 0xC6, 0x26, 0xA6, 0x66, 0xE6,
	/* 0x68 */ 0x16, 0x96, 0x56, 0xD6, 0x36, 0xB6, 0x76, 0xF6,
	/* 0x70 */ 0x0E, 0x8E, 0x4E, 0xCE, 0x2E, 0xAE, 0x6E, 0xEE,
	/* 0x78 */ 0x1E, 0x9E, 0x5E, 0xDE, 0x3E, 0xBE, 0x7E, 0xFE,
	/* 0x80 */ 0x01, 0x81, 0x41, 0xC1, 0x21, 0xA1, 0x61, 0xE1,
	/* 0x88 */ 0x11, 0x91, 0x51, 0xD1, 0x31, 0xB1, 0x71, 0xF1,
	/* 0x90 */ 0x09, 0x89, 0x49, 0xC9, 0x29, 0xA9, 0x69, 0xE9,
	/* 0x98 */ 0x19, 0x99, 0x59, 0xD9, 0x39, 0xB9, 0x79, 0xF9,
	/* 0xA0 */ 0x05, 0x85, 0x45, 0xC5, 0x25, 0xA5, 0x65, 0xE5,
	/* 0xA8 */ 0x15, 0x95, 0x55, 0xD5, 0x35, 0xB5, 0x75, 0xF5,
	/* 0xB0 */ 0x0D, 0x8D, 0x4D, 0xCD, 0x2D, 0xAD, 0x6D, 0xED,
	/* 0xB8 */ 0x1D, 0x9D, 0x5D, 0xDD, 0x3D, 0xBD, 0x7D, 0xFD,
	/* 0xC0 */ 0x03, 0x83, 0x43, 0xC3, 0x23, 0xA3, 0x63, 0xE3,
	/* 0xC8 */ 0x13, 0x93, 0x53, 0xD3, 0x33, 0xB3, 0x73, 0xF3,
	/* 0xD0 */ 0x0B, 0x8B, 0x4B, 0xCB, 0x2B, 0xAB, 0x6B, 0xEB,
	/* 0xD8 */ 0x1B, 0x9B, 0x5B, 0xDB, 0x3B, 0xBB, 0x7B, 0xFB,
	/* 0xE0 */ 0x07, 0x87, 0x47, 0xC7, 0x27, 0xA7, 0x67, 0xE7,
	/* 0xE8 */ 0x17, 0x97, 0x57, 0xD7, 0x37, 0xB7, 0x77, 0xF7,
	/* 0xF0 */ 0x0F, 0x8F, 0x4F, 0xCF, 0x2F, 0xAF, 0x6F, 0xEF,
	/* 0xF8 */ 0x1F, 0x9F, 0x5F, 0xDF, 0x3F, 0xBF, 0x7F, 0xFF,
};

void
haddr_conv802(addr_in, addr_out, len)
	u_char *addr_in, *addr_out;
	int len;
{
	u_char *lim;

	lim = addr_out + len;
	while (addr_out < lim)
		*addr_out++ = conv802table[*addr_in++];
}

#if 0
/*
 * For the record, here is a program to generate the
 * bit-reverse table above.
 */
static int
bitrev(n)
	int n;
{
	int i, r;

	r = 0;
	for (i = 0; i < 8; i++) {
		r <<= 1;
		r |= (n & 1);
		n >>= 1;
	}
	return r;
}

main()
{
	int i;
	for (i = 0; i <= 0xFF; i++) {
		if ((i & 7) == 0)
			printf("/* 0x%02X */", i);
		printf(" 0x%02X,", bitrev(i));
		if ((i & 7) == 7)
			printf("\n");
	}
}

#endif

/*
 * Local Variables:
 * tab-width: 4
 * c-indent-level: 4
 * c-argdecl-indent: 4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: -4
 * c-label-offset: -4
 * c-brace-offset: 0
 * End:
 */
