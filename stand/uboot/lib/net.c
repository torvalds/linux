/*-
 * Copyright (c) 2000-2001 Benno Rice
 * Copyright (c) 2007 Semihalf, Rafal Jaworowski <raj@semihalf.com>
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <net/if.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>

#include <stand.h>
#include <net.h>
#include <netif.h>

#include "api_public.h"
#include "glue.h"
#include "libuboot.h"
#include "dev_net.h"

static int	net_probe(struct netif *, void *);
static int	net_match(struct netif *, void *);
static void	net_init(struct iodesc *, void *);
static ssize_t	net_get(struct iodesc *, void **, time_t);
static ssize_t	net_put(struct iodesc *, void *, size_t);
static void	net_end(struct netif *);

extern struct netif_stats net_stats[];

struct netif_dif net_ifs[] = {
	/*	dif_unit	dif_nsel	dif_stats	dif_private */
	{	0,		1,		&net_stats[0],	0,	},
};

struct netif_stats net_stats[nitems(net_ifs)];

struct netif_driver uboot_net = {
	"uboot_eth",		/* netif_bname */
	net_match,		/* netif_match */
	net_probe,		/* netif_probe */
	net_init,		/* netif_init */
	net_get,		/* netif_get */
	net_put,		/* netif_put */
	net_end,		/* netif_end */
	net_ifs,		/* netif_ifs */
	nitems(net_ifs)		/* netif_nifs */
};

struct uboot_softc {
	uint32_t	sc_pad;
	uint8_t		sc_rxbuf[ETHER_MAX_LEN];
	uint8_t		sc_txbuf[ETHER_MAX_LEN + PKTALIGN];
	uint8_t		*sc_txbufp;
	int		sc_handle;	/* device handle for ub_dev_xxx */
};

static struct uboot_softc uboot_softc;

/*
 * get_env_net_params()
 *
 * Attempt to obtain all the parms we need for netbooting from the U-Boot
 * environment.  If we fail to obtain the values it may still be possible to
 * netboot; the net_dev code will attempt to get the values from bootp, rarp,
 * and other such sources.
 *
 * If rootip.s_addr is non-zero net_dev assumes the required global variables
 * are set and skips the bootp inquiry.  For that reason, we don't set rootip
 * until we've verified that we have at least the minimum required info.
 *
 * This is called from netif_init() which can result in it getting called
 * multiple times, by design.  The network code at higher layers zeroes out
 * rootip when it closes a network interface, so if it gets opened again we have
 * to obtain all this info again.
 */
static void
get_env_net_params()
{
	char *envstr;
	in_addr_t rootaddr, serveraddr;

	/*
	 * Silently get out right away if we don't have rootpath, because none
	 * of the other info we obtain below is sufficient to boot without it.
	 *
	 * If we do have rootpath, copy it into the global var and also set
	 * dhcp.root-path in the env.  If we don't get all the other info from
	 * the u-boot env below, we will still try dhcp/bootp, but the server-
	 * provided path will not replace the user-provided value we set here.
	 */
	if ((envstr = ub_env_get("rootpath")) == NULL)
		return;
	strlcpy(rootpath, envstr, sizeof(rootpath));
	setenv("dhcp.root-path", rootpath, 0);

	/*
	 * Our own IP address must be valid.  Silently get out if it's not set,
	 * but whine if it's there and we can't parse it.
	 */
	if ((envstr = ub_env_get("ipaddr")) == NULL)
		return;
	if ((myip.s_addr = inet_addr(envstr)) == INADDR_NONE) {
		printf("Could not parse ipaddr '%s'\n", envstr);
		return;
	}

	/*
	 * Netmask is optional, default to the "natural" netmask for our IP, but
	 * whine if it was provided and we couldn't parse it.
	 */
	if ((envstr = ub_env_get("netmask")) != NULL &&
	    (netmask = inet_addr(envstr)) == INADDR_NONE) {
		printf("Could not parse netmask '%s'\n", envstr);
	}
	if (netmask == INADDR_NONE) {
		if (IN_CLASSA(myip.s_addr))
			netmask = IN_CLASSA_NET;
		else if (IN_CLASSB(myip.s_addr))
			netmask = IN_CLASSB_NET;
		else
			netmask = IN_CLASSC_NET;
	}

	/*
	 * Get optional serverip before rootpath; the latter can override it.
	 * Whine only if it's present but can't be parsed.
	 */
	serveraddr = INADDR_NONE;
	if ((envstr = ub_env_get("serverip")) != NULL) {
		if ((serveraddr = inet_addr(envstr)) == INADDR_NONE) 
			printf("Could not parse serverip '%s'\n", envstr);
	}

	/*
	 * There must be a rootpath.  It may be ip:/path or it may be just the
	 * path in which case the ip needs to be in serverip.
	 */
	rootaddr = net_parse_rootpath();
	if (rootaddr == INADDR_NONE)
		rootaddr = serveraddr;
	if (rootaddr == INADDR_NONE) {
		printf("No server address for rootpath '%s'\n", envstr);
		return;
	}
	rootip.s_addr = rootaddr;

	/*
	 * Gateway IP is optional unless rootip is on a different net in which
	 * case whine if it's missing or we can't parse it, and set rootip addr
	 * to zero, which signals to other network code that network params
	 * aren't set (so it will try dhcp, bootp, etc).
	 */
	envstr = ub_env_get("gatewayip");
	if (!SAMENET(myip, rootip, netmask)) {
		if (envstr == NULL)  {
			printf("Need gatewayip for a root server on a "
			    "different network.\n");
			rootip.s_addr = 0;
			return;
		}
		if ((gateip.s_addr = inet_addr(envstr) == INADDR_NONE)) {
			printf("Could not parse gatewayip '%s'\n", envstr);
			rootip.s_addr = 0;
			return;
		}
	}
}

static int
net_match(struct netif *nif, void *machdep_hint)
{
	char **a = (char **)machdep_hint;

	if (memcmp("net", *a, 3) == 0)
		return (1);

	printf("net_match: could not match network device\n");
	return (0);
}

static int
net_probe(struct netif *nif, void *machdep_hint)
{
	struct device_info *di;
	int i;

	for (i = 0; i < devs_no; i++)
		if ((di = ub_dev_get(i)) != NULL)
			if (di->type == DEV_TYP_NET)
				break;

	if (i == devs_no) {
		printf("net_probe: no network devices found, maybe not"
		    " enumerated yet..?\n");
		return (-1);
	}

#if defined(NETIF_DEBUG)
	printf("net_probe: network device found: %d\n", i);
#endif
	uboot_softc.sc_handle = i;

	return (0);
}

static ssize_t
net_put(struct iodesc *desc, void *pkt, size_t len)
{
	struct netif *nif = desc->io_netif;
	struct uboot_softc *sc = nif->nif_devdata;
	size_t sendlen;
	ssize_t rv;

#if defined(NETIF_DEBUG)
	struct ether_header *eh;

	printf("net_put: desc %p, pkt %p, len %d\n", desc, pkt, len);
	eh = pkt;
	printf("dst: %s ", ether_sprintf(eh->ether_dhost));
	printf("src: %s ", ether_sprintf(eh->ether_shost));
	printf("type: 0x%x\n", eh->ether_type & 0xffff);
#endif

	if (len < ETHER_MIN_LEN - ETHER_CRC_LEN) {
		sendlen = ETHER_MIN_LEN - ETHER_CRC_LEN;
		bzero(sc->sc_txbufp, sendlen);
	} else
		sendlen = len;

	memcpy(sc->sc_txbufp, pkt, len);

	rv = ub_dev_send(sc->sc_handle, sc->sc_txbufp, sendlen);

#if defined(NETIF_DEBUG)
	printf("net_put: ub_send returned %d\n", rv);
#endif
	if (rv == 0)
		rv = len;
	else
		rv = -1;

	return (rv);
}

static ssize_t
net_get(struct iodesc *desc, void **pkt, time_t timeout)
{
	struct netif *nif = desc->io_netif;
	struct uboot_softc *sc = nif->nif_devdata;
	time_t t;
	int err, rlen;
	size_t len;
	char *buf;

#if defined(NETIF_DEBUG)
	printf("net_get: pkt %p, timeout %d\n", pkt, timeout);
#endif
	t = getsecs();
	len = sizeof(sc->sc_rxbuf);
	do {
		err = ub_dev_recv(sc->sc_handle, sc->sc_rxbuf, len, &rlen);

		if (err != 0) {
			printf("net_get: ub_dev_recv() failed, error=%d\n",
			    err);
			rlen = 0;
			break;
		}
	} while ((rlen == -1 || rlen == 0) && (getsecs() - t < timeout));

#if defined(NETIF_DEBUG)
	printf("net_get: received len %d (%x)\n", rlen, rlen);
#endif

	if (rlen > 0) {
		buf = malloc(rlen + ETHER_ALIGN);
		if (buf == NULL)
			return (-1);
		memcpy(buf + ETHER_ALIGN, sc->sc_rxbuf, rlen);
		*pkt = buf;
		return ((ssize_t)rlen);
	}

	return (-1);
}

static void
net_init(struct iodesc *desc, void *machdep_hint)
{
	struct netif *nif = desc->io_netif;
	struct uboot_softc *sc;
	struct device_info *di;
	int err;

	sc = nif->nif_devdata = &uboot_softc;

	if ((err = ub_dev_open(sc->sc_handle)) != 0)
		panic("%s%d: initialisation failed with error %d",
		    nif->nif_driver->netif_bname, nif->nif_unit, err);

	/* Get MAC address */
	di = ub_dev_get(sc->sc_handle);
	memcpy(desc->myea, di->di_net.hwaddr, 6);
	if (memcmp (desc->myea, "\0\0\0\0\0\0", 6) == 0) {
		panic("%s%d: empty ethernet address!",
		    nif->nif_driver->netif_bname, nif->nif_unit);
	}

	/* Attempt to get netboot params from the u-boot env. */
	get_env_net_params();
	if (myip.s_addr != 0)
		desc->myip = myip;

#if defined(NETIF_DEBUG)
	printf("network: %s%d attached to %s\n", nif->nif_driver->netif_bname,
	    nif->nif_unit, ether_sprintf(desc->myea));
#endif

	/* Set correct alignment for TX packets */
	sc->sc_txbufp = sc->sc_txbuf;
	if ((unsigned long)sc->sc_txbufp % PKTALIGN)
		sc->sc_txbufp += PKTALIGN -
		    (unsigned long)sc->sc_txbufp % PKTALIGN;
}

static void
net_end(struct netif *nif)
{
	struct uboot_softc *sc = nif->nif_devdata;
	int err;

	if ((err = ub_dev_close(sc->sc_handle)) != 0)
		panic("%s%d: net_end failed with error %d",
		    nif->nif_driver->netif_bname, nif->nif_unit, err);
}
