/*
 * bluetooth.c
 */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001-2009 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: bluetooth.c,v 1.3 2003/05/20 23:04:30 max Exp $
 * $FreeBSD$
 */
#define L2CAP_SOCKET_CHECKED
#include <bluetooth.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define _PATH_BT_HOSTS		"/etc/bluetooth/hosts"
#define _PATH_BT_PROTOCOLS	"/etc/bluetooth/protocols"
#define MAXALIASES		 35

static FILE		*hostf = NULL;
static int		 host_stayopen = 0;
static struct hostent	 host;
static bdaddr_t		 host_addr;
static char		*host_addr_ptrs[2];
static char		*host_aliases[MAXALIASES];

static FILE		*protof = NULL;
static int		 proto_stayopen = 0;
static struct protoent	 proto;
static char		*proto_aliases[MAXALIASES];

static char		 buf[BUFSIZ + 1];

static int bt_hex_byte   (char const *str);
static int bt_hex_nibble (char nibble);

struct hostent *
bt_gethostbyname(char const *name)
{
	struct hostent	*p;
	char		**cp;

	bt_sethostent(host_stayopen);
	while ((p = bt_gethostent()) != NULL) {
		if (strcasecmp(p->h_name, name) == 0)
			break;
		for (cp = p->h_aliases; *cp != NULL; cp++)
			if (strcasecmp(*cp, name) == 0)
				goto found;
	}
found:
	bt_endhostent();

	return (p);
}

struct hostent *
bt_gethostbyaddr(char const *addr, int len, int type)
{
	struct hostent	*p;

	if (type != AF_BLUETOOTH || len != sizeof(bdaddr_t)) {
		h_errno = NO_RECOVERY;
		return (NULL);
	}

	bt_sethostent(host_stayopen);
	while ((p = bt_gethostent()) != NULL)
		if (p->h_addrtype == type && bcmp(p->h_addr, addr, len) == 0)
			break;
	bt_endhostent();

	return (p);
}

struct hostent *
bt_gethostent(void)
{
	char	*p, *cp, **q;

	if (hostf == NULL)
		hostf = fopen(_PATH_BT_HOSTS, "r");

	if (hostf == NULL) {
		h_errno = NETDB_INTERNAL;
		return (NULL);
	}
again:
	if ((p = fgets(buf, sizeof(buf), hostf)) == NULL) {
		h_errno = HOST_NOT_FOUND;
		return (NULL);
	}
	if (*p == '#')
		goto again;
	if ((cp = strpbrk(p, "#\n")) == NULL)
		goto again;
	*cp = 0;
	if ((cp = strpbrk(p, " \t")) == NULL)
		goto again;
	*cp++ = 0;
	if (bt_aton(p, &host_addr) == 0) 
		goto again;
	host_addr_ptrs[0] = (char *) &host_addr;
	host_addr_ptrs[1] = NULL;
	host.h_addr_list = host_addr_ptrs;
	host.h_length = sizeof(host_addr);
	host.h_addrtype = AF_BLUETOOTH;
	while (*cp == ' ' || *cp == '\t')
		cp++;
	host.h_name = cp;
	q = host.h_aliases = host_aliases;
	if ((cp = strpbrk(cp, " \t")) != NULL)
		*cp++ = 0;
	while (cp != NULL && *cp != 0) {
		if (*cp == ' ' || *cp == '\t') {
			cp++;
			continue;
		}
		if (q < &host_aliases[MAXALIASES - 1])
			*q++ = cp;
		if ((cp = strpbrk(cp, " \t")) != NULL)
			*cp++ = 0;
	}
	*q = NULL;
	h_errno = NETDB_SUCCESS;

	return (&host);
}

void
bt_sethostent(int stayopen)
{
	if (hostf == NULL)
		hostf = fopen(_PATH_BT_HOSTS, "r");
	else
		rewind(hostf);

	host_stayopen = stayopen;
}

void
bt_endhostent(void)
{
	if (hostf != NULL && host_stayopen == 0) {
		(void) fclose(hostf);
		hostf = NULL;
	}
}

struct protoent *
bt_getprotobyname(char const *name)
{
	struct protoent	 *p;
	char		**cp;

	bt_setprotoent(proto_stayopen);
	while ((p = bt_getprotoent()) != NULL) {
		if (strcmp(p->p_name, name) == 0)
			break;
		for (cp = p->p_aliases; *cp != NULL; cp++)
			if (strcmp(*cp, name) == 0)
				goto found;
	}
found:
	bt_endprotoent();

	return (p);
}

struct protoent *
bt_getprotobynumber(int proto)
{
	struct protoent	*p;

	bt_setprotoent(proto_stayopen);
	while ((p = bt_getprotoent()) != NULL)
		if (p->p_proto == proto)
			break;
	bt_endprotoent();

	return (p);
}

struct protoent *
bt_getprotoent(void)
{
	char	*p, *cp, **q;

	if (protof == NULL)
		protof = fopen(_PATH_BT_PROTOCOLS, "r");

	if (protof == NULL)
		return (NULL);
again:
	if ((p = fgets(buf, sizeof(buf), protof)) == NULL)
		return (NULL);
	if (*p == '#')
		goto again;
	if ((cp = strpbrk(p, "#\n")) == NULL)
		goto again;
	*cp = '\0';
	proto.p_name = p;
	if ((cp = strpbrk(p, " \t")) == NULL)
		goto again;
	*cp++ = '\0';
	while (*cp == ' ' || *cp == '\t')
		cp++;
	if ((p = strpbrk(cp, " \t")) != NULL)
		*p++ = '\0';
	proto.p_proto = atoi(cp);
	q = proto.p_aliases = proto_aliases;
	if (p != NULL) {
		cp = p;
		while (cp != NULL && *cp != 0) {
			if (*cp == ' ' || *cp == '\t') {
				cp++;
				continue;
			}
			if (q < &proto_aliases[MAXALIASES - 1])
				*q++ = cp;
			if ((cp = strpbrk(cp, " \t")) != NULL)
				*cp++ = '\0';
		}
	}
	*q = NULL;

	return (&proto);
}

void
bt_setprotoent(int stayopen)
{
	if (protof == NULL)
		protof = fopen(_PATH_BT_PROTOCOLS, "r");
	else
		rewind(protof);

	proto_stayopen = stayopen;
}

void
bt_endprotoent(void)
{
	if (protof != NULL) {
		(void) fclose(protof);
		protof = NULL;
	}
}

char const *
bt_ntoa(bdaddr_t const *ba, char *str)
{
	static char	buffer[24];

	if (str == NULL)
		str = buffer;

	sprintf(str, "%2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x",
		ba->b[5], ba->b[4], ba->b[3], ba->b[2], ba->b[1], ba->b[0]);

	return (str);
}

int
bt_aton(char const *str, bdaddr_t *ba)
{
	int	 i, b;
	char	*end = NULL;

	memset(ba, 0, sizeof(*ba));

	for (i = 5, end = strchr(str, ':');
	     i > 0 && *str != '\0' && end != NULL;
	     i --, str = end + 1, end = strchr(str, ':')) {
		switch (end - str) {
		case 1:
			b = bt_hex_nibble(str[0]);
			break;

		case 2:
			b = bt_hex_byte(str);
			break;

		default:
			b = -1;
			break;
		}
		
		if (b < 0)
			return (0);

		ba->b[i] = b;
	}

	if (i != 0 || end != NULL || *str == 0)
		return (0);

	switch (strlen(str)) {
	case 1:
		b = bt_hex_nibble(str[0]);
		break;

	case 2:
		b = bt_hex_byte(str);
		break;

	default:
		b = -1;
		break;
	}

	if (b < 0)
		return (0);

	ba->b[i] = b;

	return (1);
}

static int
bt_hex_byte(char const *str)
{
	int	n1, n2;

	if ((n1 = bt_hex_nibble(str[0])) < 0)
		return (-1);

	if ((n2 = bt_hex_nibble(str[1])) < 0)
		return (-1);

	return ((((n1 & 0x0f) << 4) | (n2 & 0x0f)) & 0xff);
}

static int
bt_hex_nibble(char nibble)
{
	if ('0' <= nibble && nibble <= '9')
		return (nibble - '0');

	if ('a' <= nibble && nibble <= 'f')
		return (nibble - 'a' + 0xa);

	if ('A' <= nibble && nibble <= 'F')
		return (nibble - 'A' + 0xa);

	return (-1);
}

