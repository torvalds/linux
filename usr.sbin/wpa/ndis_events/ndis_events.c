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
 * This program simulates the behavior of the ndis_events utility
 * supplied with wpa_supplicant for Windows. The original utility
 * is designed to translate Windows WMI events. We don't have WMI,
 * but we need to supply certain event info to wpa_supplicant in
 * order to make WPA2 work correctly, so we fake up the interface.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <sys/sysctl.h>
#include <net/if.h>
#include <net/if_dl.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <net/route.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#include <syslog.h>
#include <stdarg.h>

static int verbose = 0;
static int debug = 0;
static int all_events = 0;

#define PROGNAME "ndis_events"

#define WPA_SUPPLICANT_PORT	9876
#define NDIS_INDICATION_LEN	2048

#define EVENT_CONNECT		0
#define EVENT_DISCONNECT	1
#define EVENT_MEDIA_SPECIFIC	2

#define NDIS_STATUS_MEDIA_CONNECT		0x4001000B
#define NDIS_STATUS_MEDIA_DISCONNECT		0x4001000C
#define NDIS_STATUS_MEDIA_SPECIFIC_INDICATION	0x40010012

struct ndis_evt {
	uint32_t		ne_sts;
	uint32_t		ne_len;
#ifdef notdef
	char			ne_buf[1];
#endif
};

static int find_ifname(int, char *);
static int announce_event(char *, int, struct sockaddr_in *);
static void usage(void);

static void
dbgmsg(const char *fmt, ...)
{
	va_list			ap;

	va_start(ap, fmt);
	if (debug)
		vwarnx(fmt, ap);
	else
		vsyslog(LOG_ERR, fmt, ap);
	va_end(ap);

	return;
}

static int
find_ifname(idx, name)
	int			idx;
	char			*name;
{
	int			mib[6];
	size_t			needed;
	struct if_msghdr	*ifm;
	struct sockaddr_dl	*sdl;
	char			*buf, *lim, *next;

	needed = 0;
	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[2] = 0;             /* protocol */
	mib[3] = 0;             /* wildcard address family */
	mib[4] = NET_RT_IFLIST;
	mib[5] = 0;             /* no flags */

	if (sysctl (mib, 6, NULL, &needed, NULL, 0) < 0)
		return(EIO);

	buf = malloc (needed);
	if (buf == NULL)
		return(ENOMEM);

	if (sysctl (mib, 6, buf, &needed, NULL, 0) < 0) {
		free(buf);
		return(EIO);
	}

	lim = buf + needed;

	next = buf;
	while (next < lim) {
		ifm = (struct if_msghdr *)next;
		if (ifm->ifm_type == RTM_IFINFO) {
			sdl = (struct sockaddr_dl *)(ifm + 1);
			if (ifm->ifm_index == idx) {
				strncpy(name, sdl->sdl_data, sdl->sdl_nlen);
				name[sdl->sdl_nlen] = '\0';
				free (buf);
				return (0);
			}
		}
		next += ifm->ifm_msglen;
	}

	free (buf);

	return(ENOENT);
}

static int 
announce_event(ifname, sock, dst)
	char			*ifname;
	int			sock;
	struct sockaddr_in	*dst;
{
	int			s;
	char			indication[NDIS_INDICATION_LEN];
        struct ifreq            ifr;
	struct ndis_evt		*e;
	char			buf[512], *pos, *end;
	int			len, type, _type;

	s = socket(PF_INET, SOCK_DGRAM, 0);

	if (s < 0) {
		dbgmsg("socket creation failed");
                return(EINVAL);
	}

        bzero((char *)&ifr, sizeof(ifr));
	e = (struct ndis_evt *)indication;
	e->ne_len = NDIS_INDICATION_LEN - sizeof(struct ndis_evt);

        strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
        ifr.ifr_data = indication;

	if (ioctl(s, SIOCGPRIVATE_0, &ifr) < 0) {
		close(s);
		if (verbose) {
			if (errno == ENOENT)
				dbgmsg("drained all events from %s",
				    ifname, errno);
			else
				dbgmsg("failed to read event info from %s: %d",
				    ifname, errno);
		}
		return(ENOENT);
	}

	if (e->ne_sts == NDIS_STATUS_MEDIA_CONNECT) {
		type = EVENT_CONNECT;
		if (verbose)
			dbgmsg("Received a connect event for %s", ifname);
		if (!all_events) {
			close(s);
			return(0);
		}
	}
	if (e->ne_sts == NDIS_STATUS_MEDIA_DISCONNECT) {
		type = EVENT_DISCONNECT;
		if (verbose)
			dbgmsg("Received a disconnect event for %s", ifname);
		if (!all_events) {
			close(s);
			return(0);
		}
	}
	if (e->ne_sts == NDIS_STATUS_MEDIA_SPECIFIC_INDICATION) {
		type = EVENT_MEDIA_SPECIFIC;
		if (verbose)
			dbgmsg("Received a media-specific event for %s",
			    ifname);
	}

	end = buf + sizeof(buf);
	_type = (int) type;
	memcpy(buf, &_type, sizeof(_type));
	pos = buf + sizeof(_type);

	len = snprintf(pos + 1, end - pos - 1, "%s", ifname);
	if (len < 0) {
		close(s);
		return(ENOSPC);
	}
	if (len > 255)
		len = 255;
	*pos = (unsigned char) len;
	pos += 1 + len;
	if (e->ne_len) {
		if (e->ne_len > 255 || 1 + e->ne_len > end - pos) {
			dbgmsg("Not enough room for send_event data (%d)\n",
			    e->ne_len);
			close(s);
			return(ENOSPC);
 		}
		*pos++ = (unsigned char) e->ne_len;
		memcpy(pos, (indication) + sizeof(struct ndis_evt), e->ne_len);
		pos += e->ne_len;
	}

	len = sendto(sock, buf, pos - buf, 0, (struct sockaddr *) dst,
	    sizeof(struct sockaddr_in));

	close(s);
	return(0);
}

static void
usage()
{
	fprintf(stderr, "Usage: ndis_events [-a] [-d] [-v]\n");
	exit(1);
}

int
main(argc, argv)
	int			argc;
	char			*argv[];
{
	int			s, r, n;
	struct sockaddr_in	sin;
	char			msg[NDIS_INDICATION_LEN];
	struct rt_msghdr	*rtm;
	struct if_msghdr	*ifm;
	char			ifname[IFNAMSIZ];
	int			ch;

	while ((ch = getopt(argc, argv, "dva")) != -1) {
		switch(ch) {
		case 'd':
			debug++;
			break;
		case 'v':
			verbose++;
			break;
		case 'a':
			all_events++;
			break;
		default:
			usage();
			break;
		}
	}

	if (!debug && daemon(0, 0))
		err(1, "failed to daemonize ourselves");

	if (!debug)
		openlog(PROGNAME, LOG_PID | LOG_CONS, LOG_DAEMON);

	bzero((char *)&sin, sizeof(sin));

	/* Create a datagram  socket. */

	s = socket(PF_INET, SOCK_DGRAM, 0);
	if (s < 0) {
		dbgmsg("socket creation failed");
		exit(1);
	}

	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = inet_addr("127.0.0.1");
	sin.sin_port = htons(WPA_SUPPLICANT_PORT);

	/* Create a routing socket. */

	r = socket (PF_ROUTE, SOCK_RAW, 0);
	if (r < 0) {
		dbgmsg("routing socket creation failed");
		exit(1);
	}

	/* Now sit and spin, waiting for events. */

	if (verbose)
		dbgmsg("Listening for events");

	while (1) {
		n = read(r, msg, NDIS_INDICATION_LEN);
		rtm = (struct rt_msghdr *)msg;
		if (rtm->rtm_type != RTM_IFINFO)
			continue;
		ifm = (struct if_msghdr *)msg;
		if (find_ifname(ifm->ifm_index, ifname))
			continue;
		if (strstr(ifname, "ndis")) {
			while(announce_event(ifname, s, &sin) == 0)
				;
		} else {
			if (verbose)
				dbgmsg("Skipping ifinfo message from %s",
				    ifname);
		}
	}

	/* NOTREACHED */
	exit(0);
}
