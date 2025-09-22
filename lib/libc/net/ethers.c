/*	$OpenBSD: ethers.c,v 1.28 2025/06/29 00:33:46 dlg Exp $	*/

/*
 * Copyright (c) 1998 Todd C. Miller <millert@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* 
 * ethers(3) a la Sun.
 * Originally Written by Roland McGrath <roland@frob.com> 10/14/93.
 * Substantially modified by Todd C. Miller <millert@openbsd.org>
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <paths.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

#ifndef _PATH_ETHERS
#define _PATH_ETHERS	"/etc/ethers"
#endif

static char * _ether_aton(const char *, struct ether_addr *);

char *
ether_ntoa(const struct ether_addr *e)
{
	static char a[] = "xx:xx:xx:xx:xx:xx";

	(void)snprintf(a, sizeof a, "%02x:%02x:%02x:%02x:%02x:%02x",
	    e->ether_addr_octet[0], e->ether_addr_octet[1],
	    e->ether_addr_octet[2], e->ether_addr_octet[3],
	    e->ether_addr_octet[4], e->ether_addr_octet[5]);

	return (a);
}

static char *
_ether_aton(const char *s, struct ether_addr *e)
{
	int i;
	long l;
	char *pp;

	while (isspace((unsigned char)*s))
		s++;

	/* expect 6 hex octets separated by ':' or space/NUL if last octet */
	for (i = 0; i < 6; i++) {
		l = strtol(s, &pp, 16);
		if (pp == s || l > 0xFF || l < 0)
			return (NULL);
		if (!(*pp == ':' ||
		    (i == 5 && (isspace((unsigned char)*pp) ||
		    *pp == '\0'))))
			return (NULL);
		e->ether_addr_octet[i] = (u_char)l;
		s = pp + 1;
	}

	/* return character after the octets ala strtol(3) */
	return (pp);
}

struct ether_addr *
ether_aton(const char *s)
{
	static struct ether_addr n;

	return (_ether_aton(s, &n) ? &n : NULL);
}

int
ether_ntohost(char *hostname, struct ether_addr *e)
{
	FILE *f; 
	char buf[BUFSIZ+1], *p;
	size_t len;
	struct ether_addr try;

	f = fopen(_PATH_ETHERS, "re");
	if (f == NULL)
		return (-1);
	while ((p = fgetln(f, &len)) != NULL) {
		if (p[len-1] == '\n')
			len--;
		if (len > sizeof(buf) - 2)
			continue;
		(void)memcpy(buf, p, len);
		buf[len] = '\n';	/* code assumes newlines later on */
		buf[len+1] = '\0';
		/* A + in the file meant try YP, ignore it. */
		if (!strncmp(buf, "+\n", sizeof(buf)))
			continue;
		if (ether_line(buf, &try, hostname) == 0 &&
		    memcmp(&try, e, sizeof(try)) == 0) {
			(void)fclose(f);
			return (0);
		}     
	}
	(void)fclose(f);
	errno = ENOENT;
	return (-1);
}

int
ether_hostton(const char *hostname, struct ether_addr *e)
{
	FILE *f;
	char buf[BUFSIZ+1], *p;
	char try[HOST_NAME_MAX+1];
	size_t len;

	f = fopen(_PATH_ETHERS, "re");
	if (f==NULL)
		return (-1);

	while ((p = fgetln(f, &len)) != NULL) {
		if (p[len-1] == '\n')
			len--;
		if (len > sizeof(buf) - 2)
			continue;
		memcpy(buf, p, len);
		buf[len] = '\n';	/* code assumes newlines later on */
		buf[len+1] = '\0';
		/* A + in the file meant try YP, ignore it. */
		if (!strncmp(buf, "+\n", sizeof(buf)))
			continue;
		if (ether_line(buf, e, try) == 0 && strcmp(hostname, try) == 0) {
			(void)fclose(f);
			return (0);
		}
	}
	(void)fclose(f);
	errno = ENOENT;
	return (-1);
}

int
ether_line(const char *line, struct ether_addr *e, char *hostname)
{
	char *p;
	size_t n;

	/* Parse "xx:xx:xx:xx:xx:xx" */
	if ((p = _ether_aton(line, e)) == NULL || (*p != ' ' && *p != '\t'))
		goto bad;

	/* Now get the hostname */
	while (isspace((unsigned char)*p))
		p++;
	if (*p == '\0')
		goto bad;
	n = strcspn(p, " \t\n");
	if (n >= HOST_NAME_MAX+1)
		goto bad;
	strlcpy(hostname, p, n + 1);
	return (0);

bad:
	errno = EINVAL;
	return (-1);
}
DEF_WEAK(ether_line);
