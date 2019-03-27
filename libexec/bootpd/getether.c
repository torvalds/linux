/*
 * getether.c : get the ethernet address of an interface
 *
 * All of this code is quite system-specific.  As you may well
 * guess, it took a good bit of detective work to figure out!
 *
 * If you figure out how to do this on another system,
 * please let me know.  <gwr@mc.com>
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/socket.h>

#ifndef	NO_UNISTD
#include <unistd.h>
#endif

#include <ctype.h>
#include <paths.h>
#include <string.h>
#include <syslog.h>

#include "getether.h"
#include "report.h"
#define EALEN 6

#if defined(ultrix) || (defined(__osf__) && defined(__alpha))
/*
 * This is really easy on Ultrix!  Thanks to
 * Harald Lundberg <hl@tekla.fi> for this code.
 *
 * The code here is not specific to the Alpha, but that was the
 * only symbol we could find to identify DEC's version of OSF.
 * (Perhaps we should just define DEC in the Makefile... -gwr)
 */

#include <sys/ioctl.h>
#include <net/if.h>				/* struct ifdevea */

getether(ifname, eap)
	char *ifname, *eap;
{
	int rc = -1;
	int fd;
	struct ifdevea phys;
	bzero(&phys, sizeof(phys));
	strcpy(phys.ifr_name, ifname);
	if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		report(LOG_ERR, "getether: socket(INET,DGRAM) failed");
		return -1;
	}
	if (ioctl(fd, SIOCRPHYSADDR, &phys) < 0) {
		report(LOG_ERR, "getether: ioctl SIOCRPHYSADDR failed");
	} else {
		bcopy(&phys.current_pa[0], eap, EALEN);
		rc = 0;
	}
	close(fd);
	return rc;
}

#define	GETETHER
#endif /* ultrix|osf1 */


#ifdef	SUNOS

#include <sys/sockio.h>
#include <sys/time.h>			/* needed by net_if.h */
#include <net/nit_if.h>			/* for NIOCBIND */
#include <net/if.h>				/* for struct ifreq */

getether(ifname, eap)
	char *ifname;				/* interface name from ifconfig structure */
	char *eap;					/* Ether address (output) */
{
	int rc = -1;

	struct ifreq ifrnit;
	int nit;

	bzero((char *) &ifrnit, sizeof(ifrnit));
	strlcpy(&ifrnit.ifr_name[0], ifname, IFNAMSIZ);

	nit = open("/dev/nit", 0);
	if (nit < 0) {
		report(LOG_ERR, "getether: open /dev/nit: %s",
			   get_errmsg());
		return rc;
	}
	do {
		if (ioctl(nit, NIOCBIND, &ifrnit) < 0) {
			report(LOG_ERR, "getether: NIOCBIND on nit");
			break;
		}
		if (ioctl(nit, SIOCGIFADDR, &ifrnit) < 0) {
			report(LOG_ERR, "getether: SIOCGIFADDR on nit");
			break;
		}
		bcopy(&ifrnit.ifr_addr.sa_data[0], eap, EALEN);
		rc = 0;
	} while (0);
	close(nit);
	return rc;
}

#define	GETETHER
#endif /* SUNOS */


#if defined(__FreeBSD__) || defined(__NetBSD__)
/* Thanks to John Brezak <brezak@ch.hp.com> for this code. */
#include <sys/ioctl.h>
#include <sys/time.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>

int
getether(ifname, eap)
	char *ifname;				/* interface name from ifconfig structure */
	char *eap;					/* Ether address (output) */
{
	int fd, rc = -1;
	int n;
	struct ifreq ibuf[16];
	struct ifconf ifc;
	struct ifreq *ifrp, *ifend;

	/* Fetch the interface configuration */
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		report(LOG_ERR, "getether: socket %s: %s", ifname, get_errmsg());
		return (fd);
	}
	ifc.ifc_len = sizeof(ibuf);
	ifc.ifc_buf = (caddr_t) ibuf;
	if (ioctl(fd, SIOCGIFCONF, (char *) &ifc) < 0 ||
		ifc.ifc_len < sizeof(struct ifreq)) {
		report(LOG_ERR, "getether: SIOCGIFCONF: %s", get_errmsg());
		goto out;
	}
	/* Search interface configuration list for link layer address. */
	ifrp = ibuf;
	ifend = (struct ifreq *) ((char *) ibuf + ifc.ifc_len);
	while (ifrp < ifend) {
		/* Look for interface */
		if (strcmp(ifname, ifrp->ifr_name) == 0 &&
			ifrp->ifr_addr.sa_family == AF_LINK &&
		((struct sockaddr_dl *) &ifrp->ifr_addr)->sdl_type == IFT_ETHER) {
			bcopy(LLADDR((struct sockaddr_dl *) &ifrp->ifr_addr), eap, EALEN);
			rc = 0;
			break;
		}
		/* Bump interface config pointer */
		n = ifrp->ifr_addr.sa_len + sizeof(ifrp->ifr_name);
		if (n < sizeof(*ifrp))
			n = sizeof(*ifrp);
		ifrp = (struct ifreq *) ((char *) ifrp + n);
	}

  out:
	close(fd);
	return (rc);
}

#define	GETETHER
#endif /* __NetBSD__ */


#ifdef	SVR4
/*
 * This is for "Streams TCP/IP" by Lachman Associates.
 * They sure made this cumbersome!  -gwr
 */

#include <sys/sockio.h>
#include <sys/dlpi.h>
#include <stropts.h>
#include <string.h>
#ifndef NULL
#define NULL 0
#endif

int
getether(ifname, eap)
	char *ifname;				/* interface name from ifconfig structure */
	char *eap;					/* Ether address (output) */
{
	int rc = -1;
	char devname[32];
	char tmpbuf[sizeof(union DL_primitives) + 16];
	struct strbuf cbuf;
	int fd, flags;
	union DL_primitives *dlp;
	char *enaddr;
	int unit = -1;				/* which unit to attach */

	snprintf(devname, sizeof(devname), "%s%s", _PATH_DEV, ifname);
	fd = open(devname, 2);
	if (fd < 0) {
		/* Try without the trailing digit. */
		char *p = devname + 5;
		while (isalpha(*p))
			p++;
		if (isdigit(*p)) {
			unit = *p - '0';
			*p = '\0';
		}
		fd = open(devname, 2);
		if (fd < 0) {
			report(LOG_ERR, "getether: open %s: %s",
				   devname, get_errmsg());
			return rc;
		}
	}
#ifdef	DL_ATTACH_REQ
	/*
	 * If this is a "Style 2" DLPI, then we must "attach" first
	 * to tell the driver which unit (board, port) we want.
	 * For now, decide this based on the device name.
	 * (Should do "info_req" and check dl_provider_style ...)
	 */
	if (unit >= 0) {
		memset(tmpbuf, 0, sizeof(tmpbuf));
		dlp = (union DL_primitives *) tmpbuf;
		dlp->dl_primitive = DL_ATTACH_REQ;
		dlp->attach_req.dl_ppa = unit;
		cbuf.buf = tmpbuf;
		cbuf.len = DL_ATTACH_REQ_SIZE;
		if (putmsg(fd, &cbuf, NULL, 0) < 0) {
			report(LOG_ERR, "getether: attach: putmsg: %s", get_errmsg());
			goto out;
		}
		/* Recv the ack. */
		cbuf.buf = tmpbuf;
		cbuf.maxlen = sizeof(tmpbuf);
		flags = 0;
		if (getmsg(fd, &cbuf, NULL, &flags) < 0) {
			report(LOG_ERR, "getether: attach: getmsg: %s", get_errmsg());
			goto out;
		}
		/*
		 * Check the type, etc.
		 */
		if (dlp->dl_primitive == DL_ERROR_ACK) {
			report(LOG_ERR, "getether: attach: dlpi_errno=%d, unix_errno=%d",
				   dlp->error_ack.dl_errno,
				   dlp->error_ack.dl_unix_errno);
			goto out;
		}
		if (dlp->dl_primitive != DL_OK_ACK) {
			report(LOG_ERR, "getether: attach: not OK or ERROR");
			goto out;
		}
	} /* unit >= 0 */
#endif	/* DL_ATTACH_REQ */

	/*
	 * Get the Ethernet address the same way the ARP module
	 * does when it is pushed onto a new stream (bind).
	 * One should instead be able just do a dl_info_req
	 * but many drivers do not supply the hardware address
	 * in the response to dl_info_req (they MUST supply it
	 * for dl_bind_ack because the ARP module requires it).
	 */
	memset(tmpbuf, 0, sizeof(tmpbuf));
	dlp = (union DL_primitives *) tmpbuf;
	dlp->dl_primitive = DL_BIND_REQ;
	dlp->bind_req.dl_sap = 0x8FF;	/* XXX - Unused SAP */
	cbuf.buf = tmpbuf;
	cbuf.len = DL_BIND_REQ_SIZE;
	if (putmsg(fd, &cbuf, NULL, 0) < 0) {
		report(LOG_ERR, "getether: bind: putmsg: %s", get_errmsg());
		goto out;
	}
	/* Recv the ack. */
	cbuf.buf = tmpbuf;
	cbuf.maxlen = sizeof(tmpbuf);
	flags = 0;
	if (getmsg(fd, &cbuf, NULL, &flags) < 0) {
		report(LOG_ERR, "getether: bind: getmsg: %s", get_errmsg());
		goto out;
	}
	/*
	 * Check the type, etc.
	 */
	if (dlp->dl_primitive == DL_ERROR_ACK) {
		report(LOG_ERR, "getether: bind: dlpi_errno=%d, unix_errno=%d",
			   dlp->error_ack.dl_errno,
			   dlp->error_ack.dl_unix_errno);
		goto out;
	}
	if (dlp->dl_primitive != DL_BIND_ACK) {
		report(LOG_ERR, "getether: bind: not OK or ERROR");
		goto out;
	}
	if (dlp->bind_ack.dl_addr_offset == 0) {
		report(LOG_ERR, "getether: bind: ack has no address");
		goto out;
	}
	if (dlp->bind_ack.dl_addr_length < EALEN) {
		report(LOG_ERR, "getether: bind: ack address truncated");
		goto out;
	}
	/*
	 * Copy the Ethernet address out of the message.
	 */
	enaddr = tmpbuf + dlp->bind_ack.dl_addr_offset;
	memcpy(eap, enaddr, EALEN);
	rc = 0;

  out:
	close(fd);
	return rc;
}

#define	GETETHER
#endif /* SVR4 */


#ifdef	__linux__
/*
 * This is really easy on Linux!  This version (for linux)
 * written by Nigel Metheringham <nigelm@ohm.york.ac.uk> and
 * updated by Pauline Middelink <middelin@polyware.iaf.nl>
 *
 * The code is almost identical to the Ultrix code - however
 * the names are different to confuse the innocent :-)
 * Most of this code was stolen from the Ultrix bit above.
 */

#include <memory.h>
#include <sys/ioctl.h>
#include <net/if.h>	       	/* struct ifreq */
#include <sys/socketio.h>	/* Needed for IOCTL defs */

int
getether(ifname, eap)
	char *ifname, *eap;
{
	int rc = -1;
	int fd;
	struct ifreq phys;

	memset(&phys, 0, sizeof(phys));
	strcpy(phys.ifr_name, ifname);
	if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		report(LOG_ERR, "getether: socket(INET,DGRAM) failed");
		return -1;
	}
	if (ioctl(fd, SIOCGIFHWADDR, &phys) < 0) {
		report(LOG_ERR, "getether: ioctl SIOCGIFHWADDR failed");
	} else {
		memcpy(eap, &phys.ifr_hwaddr.sa_data, EALEN);
		rc = 0;
	}
	close(fd);
	return rc;
}

#define	GETETHER
#endif	/* __linux__ */


/* If we don't know how on this system, just return an error. */
#ifndef	GETETHER
int
getether(ifname, eap)
	char *ifname, *eap;
{
	return -1;
}

#endif /* !GETETHER */

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
