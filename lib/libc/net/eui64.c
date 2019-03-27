/*
 * Copyright 2004 The Aerospace Corporation.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions, and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions, and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  The name of The Aerospace Corporation may not be used to endorse or
 *     promote products derived from this software.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AEROSPACE CORPORATION "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AEROSPACE CORPORATION BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Copyright (c) 1995
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
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
 * EUI-64 conversion and lookup routines
 *
 *
 * Converted from ether_addr.c rev
 * FreeBSD: src/lib/libc/net/eui64.c,v 1.15 2002/04/08 07:51:10 ru Exp
 * by Brooks Davis
 *
 * Written by Bill Paul <wpaul@ctr.columbia.edu>
 * Center for Telecommunications Research
 * Columbia University, New York City
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stdio.h>
#include <paths.h>
#include <sys/param.h>
#include <sys/eui64.h>
#include <string.h>
#include <stdlib.h>
#ifdef YP
#include <rpc/rpc.h>
#include <rpcsvc/yp_prot.h>
#include <rpcsvc/ypclnt.h>
#endif

#ifndef _PATH_EUI64
#define _PATH_EUI64 "/etc/eui64"
#endif

static int eui64_line(const char *l, struct eui64 *e, char *hostname,
    size_t len);

/*
 * Parse a string of text containing an EUI-64 and hostname
 * and separate it into its component parts.
 */
static int
eui64_line(const char *l, struct eui64 *e, char *hostname, size_t len)
{
	char *line, *linehead, *cur;

	linehead = strdup(l);
	if (linehead == NULL)
		return (-1);
	line = linehead;

	/* Find and parse the EUI64 */
	while ((cur = strsep(&line, " \t\r\n")) != NULL) {
		if (*cur != '\0') {
			if (eui64_aton(cur, e) == 0)
				break;
			else
				goto bad;
		}
	}

	/* Find the hostname */
	while ((cur = strsep(&line, " \t\r\n")) != NULL) {
		if (*cur != '\0') {
			if (strlcpy(hostname, cur, len) <= len)
				break;
			else
				goto bad;
		}
	}

	/* Make sure what remains is either whitespace or a comment */
	while ((cur = strsep(&line, " \t\r\n")) != NULL) {
		if (*cur == '#')
			break;
		if (*cur != '\0')
			goto bad;
	}

	free(linehead);
	return (0);

bad:
	free(linehead);
	return (-1);
}

/*
 * Convert an ASCII representation of an EUI-64 to binary form.
 */
int
eui64_aton(const char *a, struct eui64 *e)
{
	int i;
	unsigned int o0, o1, o2, o3, o4, o5, o6, o7;

	/* canonical form */
	i = sscanf(a, "%x-%x-%x-%x-%x-%x-%x-%x",
	    &o0, &o1, &o2, &o3, &o4, &o5, &o6, &o7);
	if (i == EUI64_LEN)
		goto good;
	/* ethernet form */
	i = sscanf(a, "%x:%x:%x:%x:%x:%x:%x:%x",
	    &o0, &o1, &o2, &o3, &o4, &o5, &o6, &o7);
	if (i == EUI64_LEN)
		goto good;
	/* classic fwcontrol/dconschat form */
	i = sscanf(a, "0x%2x%2x%2x%2x%2x%2x%2x%2x",
	    &o0, &o1, &o2, &o3, &o4, &o5, &o6, &o7);
	if (i == EUI64_LEN)
		goto good;
	/* MAC format (-) */
	i = sscanf(a, "%x-%x-%x-%x-%x-%x",
	    &o0, &o1, &o2, &o5, &o6, &o7);
	if (i == 6) {
		o3 = 0xff;
		o4 = 0xfe;
		goto good;
	}
	/* MAC format (:) */
	i = sscanf(a, "%x:%x:%x:%x:%x:%x",
	    &o0, &o1, &o2, &o5, &o6, &o7);
	if (i == 6) {
		o3 = 0xff;
		o4 = 0xfe;
		goto good;
	}

	return (-1);

good:
        e->octet[0]=o0;
	e->octet[1]=o1;
	e->octet[2]=o2;
	e->octet[3]=o3;
	e->octet[4]=o4;
	e->octet[5]=o5;
	e->octet[6]=o6;
	e->octet[7]=o7;

        return (0);
}

/*
 * Convert a binary representation of an EUI-64 to an ASCII string.
 */
int
eui64_ntoa(const struct eui64 *id, char *a, size_t len)
{
        int i;

        i = snprintf(a, len, "%02x-%02x-%02x-%02x-%02x-%02x-%02x-%02x",
	    id->octet[0], id->octet[1], id->octet[2], id->octet[3],
	    id->octet[4], id->octet[5], id->octet[6], id->octet[7]);
        if (i < 23 || i >= len)
                return (-1);
        return (0);
}

/*
 * Map an EUI-64 to a hostname. Use either /etc/eui64 or NIS/YP.
 */
int
eui64_ntohost(char *hostname, size_t len, const struct eui64 *id)
{
	FILE *fp;
	char buf[BUFSIZ + 2];
	struct eui64 local_eui64;
	char local_host[MAXHOSTNAMELEN];
#ifdef YP
	char *result;
	int resultlen;
	char eui64_a[24];
	char *yp_domain;
#endif
	if ((fp = fopen(_PATH_EUI64, "re")) == NULL)
		return (1);

	while (fgets(buf,BUFSIZ,fp)) {
		if (buf[0] == '#')
			continue;
#ifdef YP
		if (buf[0] == '+') {
			if (yp_get_default_domain(&yp_domain))
				continue;
			eui64_ntoa(id, eui64_a, sizeof(eui64_a));
			if (yp_match(yp_domain, "eui64.byid", eui64_a,
				strlen(eui64_a), &result, &resultlen)) {
				continue;
			}
			strncpy(buf, result, resultlen);
			buf[resultlen] = '\0';
			free(result);
		}
#endif
		if (eui64_line(buf, &local_eui64, local_host,
		    sizeof(local_host)) == 0) {
			if (bcmp(&local_eui64.octet[0],
				&id->octet[0], EUI64_LEN) == 0) {
			/* We have a match */
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
 * Map a hostname to an EUI-64 using /etc/eui64 or NIS/YP.
 */
int
eui64_hostton(const char *hostname, struct eui64 *id)
{
	FILE *fp;
	char buf[BUFSIZ + 2];
	struct eui64 local_eui64;
	char local_host[MAXHOSTNAMELEN];
#ifdef YP
	char *result;
	int resultlen;
	char *yp_domain;
#endif
	if ((fp = fopen(_PATH_EUI64, "re")) == NULL)
		return (1);

	while (fgets(buf,BUFSIZ,fp)) {
		if (buf[0] == '#')
			continue;
#ifdef YP
		if (buf[0] == '+') {
			if (yp_get_default_domain(&yp_domain))
				continue;
			if (yp_match(yp_domain, "eui64.byname", hostname,
				strlen(hostname), &result, &resultlen)) {
				continue;
			}
			strncpy(buf, result, resultlen);
			buf[resultlen] = '\0';
			free(result);
		}
#endif
		if (eui64_line(buf, &local_eui64, local_host,
		    sizeof(local_host)) == 0) {
			if (strcmp(hostname, local_host) == 0) {
				/* We have a match */
				bcopy(&local_eui64, id, sizeof(struct eui64));
				fclose(fp);
				return(0);
			}
		}
	}
	fclose(fp);
	return (1);
}
