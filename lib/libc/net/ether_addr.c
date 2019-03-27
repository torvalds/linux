/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1995 Bill Paul <wpaul@ctr.columbia.edu>.
 * Copyright (c) 2007 Robert N. M. Watson
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * ethernet address conversion and lookup routines
 *
 * Written by Bill Paul <wpaul@ctr.columbia.edu>
 * Center for Telecommunications Research
 * Columbia University, New York City
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/socket.h>

#include <net/ethernet.h>

#ifdef YP
#include <rpc/rpc.h>
#include <rpcsvc/yp_prot.h>
#include <rpcsvc/ypclnt.h>
#endif

#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _PATH_ETHERS
#define	_PATH_ETHERS	"/etc/ethers"
#endif

/*
 * Parse a string of text containing an ethernet address and hostname and
 * separate it into its component parts.
 */
int
ether_line(const char *l, struct ether_addr *e, char *hostname)
{
	int i, o[6];

	i = sscanf(l, "%x:%x:%x:%x:%x:%x %s", &o[0], &o[1], &o[2], &o[3],
	    &o[4], &o[5], hostname);
	if (i == 7) {
		for (i = 0; i < 6; i++)
			e->octet[i] = o[i];
		return (0);
	} else {
		return (-1);
	}
}

/*
 * Convert an ASCII representation of an ethernet address to binary form.
 */
struct ether_addr *
ether_aton_r(const char *a, struct ether_addr *e)
{
	int i;
	unsigned int o0, o1, o2, o3, o4, o5;

	i = sscanf(a, "%x:%x:%x:%x:%x:%x", &o0, &o1, &o2, &o3, &o4, &o5);
	if (i != 6)
		return (NULL);
	e->octet[0]=o0;
	e->octet[1]=o1;
	e->octet[2]=o2;
	e->octet[3]=o3;
	e->octet[4]=o4;
	e->octet[5]=o5;
	return (e);
}

struct ether_addr *
ether_aton(const char *a)
{
	static struct ether_addr e;

	return (ether_aton_r(a, &e));
}

/*
 * Convert a binary representation of an ethernet address to an ASCII string.
 */
char *
ether_ntoa_r(const struct ether_addr *n, char *a)
{
	int i;

	i = sprintf(a, "%02x:%02x:%02x:%02x:%02x:%02x", n->octet[0],
	    n->octet[1], n->octet[2], n->octet[3], n->octet[4], n->octet[5]);
	if (i < 17)
		return (NULL);
	return (a);
}

char *
ether_ntoa(const struct ether_addr *n)
{
	static char a[18];

	return (ether_ntoa_r(n, a));
}

/*
 * Map an ethernet address to a hostname. Use either /etc/ethers or NIS/YP.
 */
int
ether_ntohost(char *hostname, const struct ether_addr *e)
{
	FILE *fp;
	char buf[BUFSIZ + 2];
	struct ether_addr local_ether;
	char local_host[MAXHOSTNAMELEN];
#ifdef YP
	char *result;
	int resultlen;
	char *ether_a;
	char *yp_domain;
#endif

	if ((fp = fopen(_PATH_ETHERS, "re")) == NULL)
		return (1);
	while (fgets(buf,BUFSIZ,fp)) {
		if (buf[0] == '#')
			continue;
#ifdef YP
		if (buf[0] == '+') {
			if (yp_get_default_domain(&yp_domain))
				continue;
			ether_a = ether_ntoa(e);
			if (yp_match(yp_domain, "ethers.byaddr", ether_a,
			    strlen(ether_a), &result, &resultlen)) {
				continue;
			}
			strncpy(buf, result, resultlen);
			buf[resultlen] = '\0';
			free(result);
		}
#endif
		if (!ether_line(buf, &local_ether, local_host)) {
			if (!bcmp((char *)&local_ether.octet[0],
			    (char *)&e->octet[0], 6)) {
				/* We have a match. */
				strcpy(hostname, local_host);
				fclose(fp);
				return(0);
			}
		}
	}
	fclose(fp);
	return (1);
}

/*
 * Map a hostname to an ethernet address using /etc/ethers or NIS/YP.
 */
int
ether_hostton(const char *hostname, struct ether_addr *e)
{
	FILE *fp;
	char buf[BUFSIZ + 2];
	struct ether_addr local_ether;
	char local_host[MAXHOSTNAMELEN];
#ifdef YP
	char *result;
	int resultlen;
	char *yp_domain;
#endif

	if ((fp = fopen(_PATH_ETHERS, "re")) == NULL)
		return (1);
	while (fgets(buf,BUFSIZ,fp)) {
		if (buf[0] == '#')
			continue;
#ifdef YP
		if (buf[0] == '+') {
			if (yp_get_default_domain(&yp_domain))
				continue;
			if (yp_match(yp_domain, "ethers.byname", hostname,
			    strlen(hostname), &result, &resultlen)) {
				continue;
			}
			strncpy(buf, result, resultlen);
			buf[resultlen] = '\0';
			free(result);
		}
#endif
		if (!ether_line(buf, &local_ether, local_host)) {
			if (!strcmp(hostname, local_host)) {
				/* We have a match. */
				bcopy((char *)&local_ether.octet[0],
				    (char *)&e->octet[0], 6);
				fclose(fp);
				return(0);
			}
		}
	}
	fclose(fp);
	return (1);
}
