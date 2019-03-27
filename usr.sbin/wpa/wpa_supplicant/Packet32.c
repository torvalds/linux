/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2005
 *      Bill Paul <wpaul@windriver.com>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * This file implements a small portion of the Winpcap API for the
 * Windows NDIS interface in wpa_supplicant. It provides just enough
 * routines to fool wpa_supplicant into thinking it's really running
 * in a Windows environment.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <sys/sysctl.h>
#include <sys/fcntl.h>
#include <net/if.h>
#include <net/if_dl.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <net/route.h>

#include <net80211/ieee80211_ioctl.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pcap.h>

#include "Packet32.h"

#define OID_802_11_ADD_KEY      0x0d01011D

typedef ULONGLONG NDIS_802_11_KEY_RSC;
typedef UCHAR NDIS_802_11_MAC_ADDRESS[6];

typedef struct NDIS_802_11_KEY {
	ULONG Length;
	ULONG KeyIndex;
	ULONG KeyLength;
	NDIS_802_11_MAC_ADDRESS BSSID;
	NDIS_802_11_KEY_RSC KeyRSC;
	UCHAR KeyMaterial[1];
} NDIS_802_11_KEY;

typedef struct NDIS_802_11_KEY_COMPAT {
	ULONG Length;
	ULONG KeyIndex;
	ULONG KeyLength;
	NDIS_802_11_MAC_ADDRESS BSSID;
	UCHAR Pad[6]; /* Make struct layout match Windows. */
	NDIS_802_11_KEY_RSC KeyRSC;
#ifdef notdef
	UCHAR KeyMaterial[1];
#endif
} NDIS_802_11_KEY_COMPAT;

#define TRUE 1
#define FALSE 0

struct adapter {
	int			socket;
	char			name[IFNAMSIZ];
	int			prev_roaming;
};

PCHAR
PacketGetVersion(void)
{
	return("FreeBSD WinPcap compatibility shim v1.0");
}

void *
PacketOpenAdapter(CHAR *iface)
{
	struct adapter		*a;
	int			s;
	int			ifflags;
	struct ifreq		ifr;
	struct ieee80211req	ireq;

	s = socket(PF_INET, SOCK_DGRAM, 0);

	if (s == -1)
		return(NULL);

	a = malloc(sizeof(struct adapter));
	if (a == NULL)
		return(NULL);

	a->socket = s;
	if (strncmp(iface, "\\Device\\NPF_", 12) == 0)
		iface += 12;
	else if (strncmp(iface, "\\DEVICE\\", 8) == 0)
		iface += 8;
	snprintf(a->name, IFNAMSIZ, "%s", iface);

	/* Turn off net80211 roaming */
	bzero((char *)&ireq, sizeof(ireq));
	strncpy(ireq.i_name, iface, sizeof (ifr.ifr_name));
	ireq.i_type = IEEE80211_IOC_ROAMING;
	if (ioctl(a->socket, SIOCG80211, &ireq) == 0) {
		a->prev_roaming = ireq.i_val;
		ireq.i_val = IEEE80211_ROAMING_MANUAL;
		if (ioctl(a->socket, SIOCS80211, &ireq) < 0)
			fprintf(stderr,
			    "Could not set IEEE80211_ROAMING_MANUAL\n");
	}

	bzero((char *)&ifr, sizeof(ifr));
        strncpy(ifr.ifr_name, iface, sizeof (ifr.ifr_name));
        if (ioctl(a->socket, SIOCGIFFLAGS, (caddr_t)&ifr) < 0) {
		free(a);
		close(s);
		return(NULL);
        }
        ifr.ifr_flags |= IFF_UP;
        if (ioctl(a->socket, SIOCSIFFLAGS, (caddr_t)&ifr) < 0) {
		free(a);
		close(s);
		return(NULL);
        }

	return(a);
}

int
PacketRequest(void *iface, BOOLEAN set, PACKET_OID_DATA *oid)
{
	struct adapter		*a;
	uint32_t		retval;
	struct ifreq		ifr;
	NDIS_802_11_KEY		*old;
	NDIS_802_11_KEY_COMPAT	*new;
	PACKET_OID_DATA		*o = NULL;

	if (iface == NULL)
		return(-1);

	a = iface;
	bzero((char *)&ifr, sizeof(ifr));

	/*
	 * This hack is necessary to work around a difference
	 * betwee the GNU C and Microsoft C compilers. The NDIS_802_11_KEY
	 * structure has a uint64_t in it, right after an array of
	 * chars. The Microsoft compiler inserts padding right before
	 * the 64-bit value to align it on a 64-bit boundary, but
	 * GCC only aligns it on a 32-bit boundary. Trying to pass
	 * the GCC-formatted structure to an NDIS binary driver
	 * fails because some of the fields appear to be at the
	 * wrong offsets.
	 *
	 * To get around this, if we detect someone is trying to do
	 * a set operation on OID_802_11_ADD_KEY, we shuffle the data
	 * into a properly padded structure and pass that into the
	 * driver instead. This allows the driver_ndis.c code supplied
	 * with wpa_supplicant to work unmodified.
	 */

	if (set == TRUE && oid->Oid == OID_802_11_ADD_KEY) {
		old = (NDIS_802_11_KEY *)&oid->Data;
		o = malloc(sizeof(PACKET_OID_DATA) +
		    sizeof(NDIS_802_11_KEY_COMPAT) + old->KeyLength);
		if (o == NULL)
			return(0);
		bzero((char *)o, sizeof(PACKET_OID_DATA) +
		    sizeof(NDIS_802_11_KEY_COMPAT) + old->KeyLength);
		o->Oid = oid->Oid;
		o->Length = sizeof(NDIS_802_11_KEY_COMPAT) + old->KeyLength;
		new = (NDIS_802_11_KEY_COMPAT *)&o->Data;
		new->KeyRSC = old->KeyRSC;
		new->Length = o->Length;
		new->KeyIndex = old->KeyIndex;
		new->KeyLength = old->KeyLength;
		bcopy(old->BSSID, new->BSSID, sizeof(NDIS_802_11_MAC_ADDRESS));
		bcopy(old->KeyMaterial, (char *)new +
		    sizeof(NDIS_802_11_KEY_COMPAT), new->KeyLength);
        	ifr.ifr_data = (caddr_t)o;
	} else
        	ifr.ifr_data = (caddr_t)oid;

        strlcpy(ifr.ifr_name, a->name, sizeof(ifr.ifr_name));

	if (set == TRUE)
		retval = ioctl(a->socket, SIOCSDRVSPEC, &ifr);
	else
		retval = ioctl(a->socket, SIOCGDRVSPEC, &ifr);

	if (o != NULL)
		free(o);

	if (retval)
		return(0);

	return(1);
}

int
PacketGetAdapterNames(CHAR *namelist, ULONG *len)
{
	int			mib[6];
	size_t			needed;
	struct if_msghdr	*ifm;
	struct sockaddr_dl	*sdl;
	char			*buf, *lim, *next;
	char			*plist;
	int			spc;
	int			i, ifcnt = 0;

	plist = namelist;
	spc = 0;

	bzero(plist, *len);

	needed = 0;
	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[2] = 0;             /* protocol */
	mib[3] = 0;             /* wildcard address family */
	mib[4] = NET_RT_IFLIST;
	mib[5] = 0;             /* no flags */

	if (sysctl (mib, 6, NULL, &needed, NULL, 0) < 0)
		return(FALSE);

	buf = malloc (needed);
	if (buf == NULL)
		return(FALSE);

	if (sysctl (mib, 6, buf, &needed, NULL, 0) < 0) {
		free(buf);
		return(FALSE);
	}

	lim = buf + needed;

	/* Generate interface name list. */

	next = buf;
	while (next < lim) {
		ifm = (struct if_msghdr *)next;
		if (ifm->ifm_type == RTM_IFINFO) {
			sdl = (struct sockaddr_dl *)(ifm + 1);
			if (strnstr(sdl->sdl_data, "wlan", sdl->sdl_nlen)) {
				if ((spc + sdl->sdl_nlen) > *len) {
					free(buf);
					return(FALSE);
				}
				strncpy(plist, sdl->sdl_data, sdl->sdl_nlen);
				plist += (sdl->sdl_nlen + 1);
				spc += (sdl->sdl_nlen + 1);
				ifcnt++;
			}
		}
		next += ifm->ifm_msglen;
	}


	/* Insert an extra "" as a spacer */

	plist++;
	spc++;

	/*
	 * Now generate the interface description list. There
	 * must be a unique description for each interface, and
	 * they have to match what the ndis_events program will
	 * feed in later. To keep this simple, we just repeat
	 * the interface list over again.
	 */

	next = buf;
	while (next < lim) {
		ifm = (struct if_msghdr *)next;
		if (ifm->ifm_type == RTM_IFINFO) {
			sdl = (struct sockaddr_dl *)(ifm + 1);
			if (strnstr(sdl->sdl_data, "wlan", sdl->sdl_nlen)) {
				if ((spc + sdl->sdl_nlen) > *len) {
					free(buf);
					return(FALSE);
				}
				strncpy(plist, sdl->sdl_data, sdl->sdl_nlen);
				plist += (sdl->sdl_nlen + 1);
				spc += (sdl->sdl_nlen + 1);
				ifcnt++;
			}
		}
		next += ifm->ifm_msglen;
	}

	free (buf);

	*len = spc + 1;

	return(TRUE);
}

void
PacketCloseAdapter(void *iface)
{	
	struct adapter		*a;
	struct ifreq		ifr;
	struct ieee80211req	ireq;

	if (iface == NULL)
		return;

	a = iface;

	/* Reset net80211 roaming */
	bzero((char *)&ireq, sizeof(ireq));
	strncpy(ireq.i_name, a->name, sizeof (ifr.ifr_name));
	ireq.i_type = IEEE80211_IOC_ROAMING;
	ireq.i_val = a->prev_roaming;
	ioctl(a->socket, SIOCS80211, &ireq);

	bzero((char *)&ifr, sizeof(ifr));
        strncpy(ifr.ifr_name, a->name, sizeof (ifr.ifr_name));
        ioctl(a->socket, SIOCGIFFLAGS, (caddr_t)&ifr);
        ifr.ifr_flags &= ~IFF_UP;
        ioctl(a->socket, SIOCSIFFLAGS, (caddr_t)&ifr);
	close(a->socket);
	free(a);

	return;
}

#if __FreeBSD_version < 600000

/*
 * The version of libpcap in FreeBSD 5.2.1 doesn't have these routines.
 * Call me insane if you will, but I still run 5.2.1 on my laptop, and
 * I'd like to use WPA there.
 */

int
pcap_get_selectable_fd(pcap_t *p)
{
	return(pcap_fileno(p));
}

/*
 * The old version of libpcap opens its BPF descriptor in read-only
 * mode. We need to temporarily create a new one we can write to.
 */

int
pcap_inject(pcap_t *p, const void *buf, size_t len)
{
	int			fd;
	int			res, n = 0;
	char			device[sizeof "/dev/bpf0000000000"];
	struct ifreq		ifr;

        /*
         * Go through all the minors and find one that isn't in use.
         */
	do {
		(void)snprintf(device, sizeof(device), "/dev/bpf%d", n++);
		fd = open(device, O_RDWR);
	} while (fd < 0 && errno == EBUSY);

	if (fd == -1)
		return(-1);

	bzero((char *)&ifr, sizeof(ifr));
	ioctl(pcap_fileno(p), BIOCGETIF, (caddr_t)&ifr);
	ioctl(fd, BIOCSETIF, (caddr_t)&ifr);

	res = write(fd, buf, len);

	close(fd);

	return(res);
}
#endif
