/*
 * getif.c : get an interface structure
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#if defined(SUNOS) || defined(SVR4)
#include <sys/sockio.h>
#endif
#ifdef	SVR4
#include <sys/stropts.h>
#endif

#include <sys/time.h>		/* for struct timeval in net/if.h */	
#include <net/if.h>				/* for struct ifreq */
#include <netinet/in.h>

#ifndef	NO_UNISTD
#include <unistd.h>
#endif
#include <syslog.h>
#include <errno.h>
#include <assert.h>

#include "getif.h"
#include "report.h"

#ifdef	__bsdi__
#define BSD 43
#endif

static struct ifreq ifreq[10];	/* Holds interface configuration */
static struct ifconf ifconf;	/* points to ifreq */

static int nmatch(u_char *ca, u_char *cb);

/* Return a pointer to the interface struct for the passed address. */
struct ifreq *
getif(s, addrp)
	int s;						/* socket file descriptor */
	struct in_addr *addrp;		/* destination address on interface */
{
	int maxmatch;
	int len, m, incr;
	struct ifreq *ifrq, *ifrmax;
	struct sockaddr_in *sip;
	char *p;

	/* If no address was supplied, just return NULL. */
	if (!addrp)
		return (struct ifreq *) 0;

	/* Get the interface config if not done already. */
	if (ifconf.ifc_len == 0) {
#ifdef	SVR4
		/*
		 * SysVr4 returns garbage if you do this the obvious way!
		 * This one took a while to figure out... -gwr
		 */
		struct strioctl ioc;
		ioc.ic_cmd = SIOCGIFCONF;
		ioc.ic_timout = 0;
		ioc.ic_len = sizeof(ifreq);
		ioc.ic_dp = (char *) ifreq;
		m = ioctl(s, I_STR, (char *) &ioc);
		ifconf.ifc_len = ioc.ic_len;
		ifconf.ifc_req = ifreq;
#else	/* SVR4 */
		ifconf.ifc_len = sizeof(ifreq);
		ifconf.ifc_req = ifreq;
		m = ioctl(s, SIOCGIFCONF, (caddr_t) & ifconf);
#endif	/* SVR4 */
		if ((m < 0) || (ifconf.ifc_len <= 0)) {
			report(LOG_ERR, "ioctl SIOCGIFCONF");
			return (struct ifreq *) 0;
		}
	}
	maxmatch = 7;				/* this many bits or less... */
	ifrmax = (struct ifreq *) 0;/* ... is not a valid match  */
	p = (char *) ifreq;
	len = ifconf.ifc_len;
	while (len > 0) {
		ifrq = (struct ifreq *) p;
		sip = (struct sockaddr_in *) &ifrq->ifr_addr;
		m = nmatch((u_char *)addrp, (u_char *)&(sip->sin_addr));
		if (m > maxmatch) {
			maxmatch = m;
			ifrmax = ifrq;
		}
#ifndef IFNAMSIZ
		/* BSD not defined or earlier than 4.3 */
		incr = sizeof(*ifrq);
#else
		incr = ifrq->ifr_addr.sa_len + IFNAMSIZ;
#endif

		p += incr;
		len -= incr;
	}

	return ifrmax;
}

/*
 * Return the number of leading bits matching in the
 * internet addresses supplied.
 */
static int
nmatch(ca, cb)
	u_char *ca, *cb;			/* ptrs to IP address, network order */
{
	u_int m = 0;				/* count of matching bits */
	u_int n = 4;				/* bytes left, then bitmask */

	/* Count matching bytes. */
	while (n && (*ca == *cb)) {
		ca++;
		cb++;
		m += 8;
		n--;
	}
	/* Now count matching bits. */
	if (n) {
		n = 0x80;
		while (n && ((*ca & n) == (*cb & n))) {
			m++;
			n >>= 1;
		}
	}
	return (m);
}

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
