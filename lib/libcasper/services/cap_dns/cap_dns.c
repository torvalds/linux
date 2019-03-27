/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012-2013 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Pawel Jakub Dawidek under sponsorship from
 * the FreeBSD Foundation.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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

#include <sys/dnv.h>
#include <sys/nv.h>
#include <netinet/in.h>

#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libcasper.h>
#include <libcasper_service.h>

#include "cap_dns.h"

static struct hostent hent;

static void
hostent_free(struct hostent *hp)
{
	unsigned int ii;

	free(hp->h_name);
	hp->h_name = NULL;
	if (hp->h_aliases != NULL) {
		for (ii = 0; hp->h_aliases[ii] != NULL; ii++)
			free(hp->h_aliases[ii]);
		free(hp->h_aliases);
		hp->h_aliases = NULL;
	}
	if (hp->h_addr_list != NULL) {
		for (ii = 0; hp->h_addr_list[ii] != NULL; ii++)
			free(hp->h_addr_list[ii]);
		free(hp->h_addr_list);
		hp->h_addr_list = NULL;
	}
}

static struct hostent *
hostent_unpack(const nvlist_t *nvl, struct hostent *hp)
{
	unsigned int ii, nitems;
	char nvlname[64];
	int n;

	hostent_free(hp);

	hp->h_name = strdup(nvlist_get_string(nvl, "name"));
	if (hp->h_name == NULL)
		goto fail;
	hp->h_addrtype = (int)nvlist_get_number(nvl, "addrtype");
	hp->h_length = (int)nvlist_get_number(nvl, "length");

	nitems = (unsigned int)nvlist_get_number(nvl, "naliases");
	hp->h_aliases = calloc(sizeof(hp->h_aliases[0]), nitems + 1);
	if (hp->h_aliases == NULL)
		goto fail;
	for (ii = 0; ii < nitems; ii++) {
		n = snprintf(nvlname, sizeof(nvlname), "alias%u", ii);
		assert(n > 0 && n < (int)sizeof(nvlname));
		hp->h_aliases[ii] =
		    strdup(nvlist_get_string(nvl, nvlname));
		if (hp->h_aliases[ii] == NULL)
			goto fail;
	}
	hp->h_aliases[ii] = NULL;

	nitems = (unsigned int)nvlist_get_number(nvl, "naddrs");
	hp->h_addr_list = calloc(sizeof(hp->h_addr_list[0]), nitems + 1);
	if (hp->h_addr_list == NULL)
		goto fail;
	for (ii = 0; ii < nitems; ii++) {
		hp->h_addr_list[ii] = malloc(hp->h_length);
		if (hp->h_addr_list[ii] == NULL)
			goto fail;
		n = snprintf(nvlname, sizeof(nvlname), "addr%u", ii);
		assert(n > 0 && n < (int)sizeof(nvlname));
		bcopy(nvlist_get_binary(nvl, nvlname, NULL),
		    hp->h_addr_list[ii], hp->h_length);
	}
	hp->h_addr_list[ii] = NULL;

	return (hp);
fail:
	hostent_free(hp);
	h_errno = NO_RECOVERY;
	return (NULL);
}

struct hostent *
cap_gethostbyname(cap_channel_t *chan, const char *name)
{

	return (cap_gethostbyname2(chan, name, AF_INET));
}

struct hostent *
cap_gethostbyname2(cap_channel_t *chan, const char *name, int type)
{
	struct hostent *hp;
	nvlist_t *nvl;

	nvl = nvlist_create(0);
	nvlist_add_string(nvl, "cmd", "gethostbyname");
	nvlist_add_number(nvl, "family", (uint64_t)type);
	nvlist_add_string(nvl, "name", name);
	nvl = cap_xfer_nvlist(chan, nvl);
	if (nvl == NULL) {
		h_errno = NO_RECOVERY;
		return (NULL);
	}
	if (nvlist_get_number(nvl, "error") != 0) {
		h_errno = (int)nvlist_get_number(nvl, "error");
		nvlist_destroy(nvl);
		return (NULL);
	}

	hp = hostent_unpack(nvl, &hent);
	nvlist_destroy(nvl);
	return (hp);
}

struct hostent *
cap_gethostbyaddr(cap_channel_t *chan, const void *addr, socklen_t len,
    int type)
{
	struct hostent *hp;
	nvlist_t *nvl;

	nvl = nvlist_create(0);
	nvlist_add_string(nvl, "cmd", "gethostbyaddr");
	nvlist_add_binary(nvl, "addr", addr, (size_t)len);
	nvlist_add_number(nvl, "family", (uint64_t)type);
	nvl = cap_xfer_nvlist(chan, nvl);
	if (nvl == NULL) {
		h_errno = NO_RECOVERY;
		return (NULL);
	}
	if (nvlist_get_number(nvl, "error") != 0) {
		h_errno = (int)nvlist_get_number(nvl, "error");
		nvlist_destroy(nvl);
		return (NULL);
	}
	hp = hostent_unpack(nvl, &hent);
	nvlist_destroy(nvl);
	return (hp);
}

static struct addrinfo *
addrinfo_unpack(const nvlist_t *nvl)
{
	struct addrinfo *ai;
	const void *addr;
	size_t addrlen;
	const char *canonname;

	addr = nvlist_get_binary(nvl, "ai_addr", &addrlen);
	ai = malloc(sizeof(*ai) + addrlen);
	if (ai == NULL)
		return (NULL);
	ai->ai_flags = (int)nvlist_get_number(nvl, "ai_flags");
	ai->ai_family = (int)nvlist_get_number(nvl, "ai_family");
	ai->ai_socktype = (int)nvlist_get_number(nvl, "ai_socktype");
	ai->ai_protocol = (int)nvlist_get_number(nvl, "ai_protocol");
	ai->ai_addrlen = (socklen_t)addrlen;
	canonname = dnvlist_get_string(nvl, "ai_canonname", NULL);
	if (canonname != NULL) {
		ai->ai_canonname = strdup(canonname);
		if (ai->ai_canonname == NULL) {
			free(ai);
			return (NULL);
		}
	} else {
		ai->ai_canonname = NULL;
	}
	ai->ai_addr = (void *)(ai + 1);
	bcopy(addr, ai->ai_addr, addrlen);
	ai->ai_next = NULL;

	return (ai);
}

int
cap_getaddrinfo(cap_channel_t *chan, const char *hostname, const char *servname,
    const struct addrinfo *hints, struct addrinfo **res)
{
	struct addrinfo *firstai, *prevai, *curai;
	unsigned int ii;
	const nvlist_t *nvlai;
	char nvlname[64];
	nvlist_t *nvl;
	int error, n;

	nvl = nvlist_create(0);
	nvlist_add_string(nvl, "cmd", "getaddrinfo");
	if (hostname != NULL)
		nvlist_add_string(nvl, "hostname", hostname);
	if (servname != NULL)
		nvlist_add_string(nvl, "servname", servname);
	if (hints != NULL) {
		nvlist_add_number(nvl, "hints.ai_flags",
		    (uint64_t)hints->ai_flags);
		nvlist_add_number(nvl, "hints.ai_family",
		    (uint64_t)hints->ai_family);
		nvlist_add_number(nvl, "hints.ai_socktype",
		    (uint64_t)hints->ai_socktype);
		nvlist_add_number(nvl, "hints.ai_protocol",
		    (uint64_t)hints->ai_protocol);
	}
	nvl = cap_xfer_nvlist(chan, nvl);
	if (nvl == NULL)
		return (EAI_MEMORY);
	if (nvlist_get_number(nvl, "error") != 0) {
		error = (int)nvlist_get_number(nvl, "error");
		nvlist_destroy(nvl);
		return (error);
	}

	nvlai = NULL;
	firstai = prevai = curai = NULL;
	for (ii = 0; ; ii++) {
		n = snprintf(nvlname, sizeof(nvlname), "res%u", ii);
		assert(n > 0 && n < (int)sizeof(nvlname));
		if (!nvlist_exists_nvlist(nvl, nvlname))
			break;
		nvlai = nvlist_get_nvlist(nvl, nvlname);
		curai = addrinfo_unpack(nvlai);
		if (curai == NULL)
			break;
		if (prevai != NULL)
			prevai->ai_next = curai;
		else if (firstai == NULL)
			firstai = curai;
		prevai = curai;
	}
	nvlist_destroy(nvl);
	if (curai == NULL && nvlai != NULL) {
		if (firstai == NULL)
			freeaddrinfo(firstai);
		return (EAI_MEMORY);
	}

	*res = firstai;
	return (0);
}

int
cap_getnameinfo(cap_channel_t *chan, const struct sockaddr *sa, socklen_t salen,
    char *host, size_t hostlen, char *serv, size_t servlen, int flags)
{
	nvlist_t *nvl;
	int error;

	nvl = nvlist_create(0);
	nvlist_add_string(nvl, "cmd", "getnameinfo");
	nvlist_add_number(nvl, "hostlen", (uint64_t)hostlen);
	nvlist_add_number(nvl, "servlen", (uint64_t)servlen);
	nvlist_add_binary(nvl, "sa", sa, (size_t)salen);
	nvlist_add_number(nvl, "flags", (uint64_t)flags);
	nvl = cap_xfer_nvlist(chan, nvl);
	if (nvl == NULL)
		return (EAI_MEMORY);
	if (nvlist_get_number(nvl, "error") != 0) {
		error = (int)nvlist_get_number(nvl, "error");
		nvlist_destroy(nvl);
		return (error);
	}

	if (host != NULL && nvlist_exists_string(nvl, "host"))
		strlcpy(host, nvlist_get_string(nvl, "host"), hostlen + 1);
	if (serv != NULL && nvlist_exists_string(nvl, "serv"))
		strlcpy(serv, nvlist_get_string(nvl, "serv"), servlen + 1);
	nvlist_destroy(nvl);
	return (0);
}

static void
limit_remove(nvlist_t *limits, const char *prefix)
{
	const char *name;
	size_t prefixlen;
	void *cookie;

	prefixlen = strlen(prefix);
again:
	cookie = NULL;
	while ((name = nvlist_next(limits, NULL, &cookie)) != NULL) {
		if (strncmp(name, prefix, prefixlen) == 0) {
			nvlist_free(limits, name);
			goto again;
		}
	}
}

int
cap_dns_type_limit(cap_channel_t *chan, const char * const *types,
    size_t ntypes)
{
	nvlist_t *limits;
	unsigned int i;
	char nvlname[64];
	int n;

	if (cap_limit_get(chan, &limits) < 0)
		return (-1);
	if (limits == NULL)
		limits = nvlist_create(0);
	else
		limit_remove(limits, "type");
	for (i = 0; i < ntypes; i++) {
		n = snprintf(nvlname, sizeof(nvlname), "type%u", i);
		assert(n > 0 && n < (int)sizeof(nvlname));
		nvlist_add_string(limits, nvlname, types[i]);
	}
	return (cap_limit_set(chan, limits));
}

int
cap_dns_family_limit(cap_channel_t *chan, const int *families,
    size_t nfamilies)
{
	nvlist_t *limits;
	unsigned int i;
	char nvlname[64];
	int n;

	if (cap_limit_get(chan, &limits) < 0)
		return (-1);
	if (limits == NULL)
		limits = nvlist_create(0);
	else
		limit_remove(limits, "family");
	for (i = 0; i < nfamilies; i++) {
		n = snprintf(nvlname, sizeof(nvlname), "family%u", i);
		assert(n > 0 && n < (int)sizeof(nvlname));
		nvlist_add_number(limits, nvlname, (uint64_t)families[i]);
	}
	return (cap_limit_set(chan, limits));
}

/*
 * Service functions.
 */
static bool
dns_allowed_type(const nvlist_t *limits, const char *type)
{
	const char *name;
	bool notypes;
	void *cookie;

	if (limits == NULL)
		return (true);

	notypes = true;
	cookie = NULL;
	while ((name = nvlist_next(limits, NULL, &cookie)) != NULL) {
		if (strncmp(name, "type", sizeof("type") - 1) != 0)
			continue;
		notypes = false;
		if (strcmp(nvlist_get_string(limits, name), type) == 0)
			return (true);
	}

	/* If there are no types at all, allow any type. */
	if (notypes)
		return (true);

	return (false);
}

static bool
dns_allowed_family(const nvlist_t *limits, int family)
{
	const char *name;
	bool nofamilies;
	void *cookie;

	if (limits == NULL)
		return (true);

	nofamilies = true;
	cookie = NULL;
	while ((name = nvlist_next(limits, NULL, &cookie)) != NULL) {
		if (strncmp(name, "family", sizeof("family") - 1) != 0)
			continue;
		nofamilies = false;
		if (family == AF_UNSPEC)
			continue;
		if (nvlist_get_number(limits, name) == (uint64_t)family)
			return (true);
	}

	/* If there are no families at all, allow any family. */
	if (nofamilies)
		return (true);

	return (false);
}

static void
hostent_pack(const struct hostent *hp, nvlist_t *nvl)
{
	unsigned int ii;
	char nvlname[64];
	int n;

	nvlist_add_string(nvl, "name", hp->h_name);
	nvlist_add_number(nvl, "addrtype", (uint64_t)hp->h_addrtype);
	nvlist_add_number(nvl, "length", (uint64_t)hp->h_length);

	if (hp->h_aliases == NULL) {
		nvlist_add_number(nvl, "naliases", 0);
	} else {
		for (ii = 0; hp->h_aliases[ii] != NULL; ii++) {
			n = snprintf(nvlname, sizeof(nvlname), "alias%u", ii);
			assert(n > 0 && n < (int)sizeof(nvlname));
			nvlist_add_string(nvl, nvlname, hp->h_aliases[ii]);
		}
		nvlist_add_number(nvl, "naliases", (uint64_t)ii);
	}

	if (hp->h_addr_list == NULL) {
		nvlist_add_number(nvl, "naddrs", 0);
	} else {
		for (ii = 0; hp->h_addr_list[ii] != NULL; ii++) {
			n = snprintf(nvlname, sizeof(nvlname), "addr%u", ii);
			assert(n > 0 && n < (int)sizeof(nvlname));
			nvlist_add_binary(nvl, nvlname, hp->h_addr_list[ii],
			    (size_t)hp->h_length);
		}
		nvlist_add_number(nvl, "naddrs", (uint64_t)ii);
	}
}

static int
dns_gethostbyname(const nvlist_t *limits, const nvlist_t *nvlin,
    nvlist_t *nvlout)
{
	struct hostent *hp;
	int family;

	if (!dns_allowed_type(limits, "NAME2ADDR") &&
	    !dns_allowed_type(limits, "NAME"))
		return (NO_RECOVERY);

	family = (int)nvlist_get_number(nvlin, "family");

	if (!dns_allowed_family(limits, family))
		return (NO_RECOVERY);

	hp = gethostbyname2(nvlist_get_string(nvlin, "name"), family);
	if (hp == NULL)
		return (h_errno);
	hostent_pack(hp, nvlout);
	return (0);
}

static int
dns_gethostbyaddr(const nvlist_t *limits, const nvlist_t *nvlin,
    nvlist_t *nvlout)
{
	struct hostent *hp;
	const void *addr;
	size_t addrsize;
	int family;

	if (!dns_allowed_type(limits, "ADDR2NAME") &&
	    !dns_allowed_type(limits, "ADDR"))
		return (NO_RECOVERY);

	family = (int)nvlist_get_number(nvlin, "family");

	if (!dns_allowed_family(limits, family))
		return (NO_RECOVERY);

	addr = nvlist_get_binary(nvlin, "addr", &addrsize);
	hp = gethostbyaddr(addr, (socklen_t)addrsize, family);
	if (hp == NULL)
		return (h_errno);
	hostent_pack(hp, nvlout);
	return (0);
}

static int
dns_getnameinfo(const nvlist_t *limits, const nvlist_t *nvlin, nvlist_t *nvlout)
{
	struct sockaddr_storage sast;
	const void *sabin;
	char *host, *serv;
	size_t sabinsize, hostlen, servlen;
	socklen_t salen;
	int error, flags;

	if (!dns_allowed_type(limits, "ADDR2NAME") &&
	    !dns_allowed_type(limits, "ADDR"))
		return (NO_RECOVERY);

	error = 0;
	host = serv = NULL;
	memset(&sast, 0, sizeof(sast));

	hostlen = (size_t)nvlist_get_number(nvlin, "hostlen");
	servlen = (size_t)nvlist_get_number(nvlin, "servlen");

	if (hostlen > 0) {
		host = calloc(1, hostlen + 1);
		if (host == NULL) {
			error = EAI_MEMORY;
			goto out;
		}
	}
	if (servlen > 0) {
		serv = calloc(1, servlen + 1);
		if (serv == NULL) {
			error = EAI_MEMORY;
			goto out;
		}
	}

	sabin = nvlist_get_binary(nvlin, "sa", &sabinsize);
	if (sabinsize > sizeof(sast)) {
		error = EAI_FAIL;
		goto out;
	}

	memcpy(&sast, sabin, sabinsize);
	salen = (socklen_t)sabinsize;

	if ((sast.ss_family != AF_INET ||
	     salen != sizeof(struct sockaddr_in)) &&
	    (sast.ss_family != AF_INET6 ||
	     salen != sizeof(struct sockaddr_in6))) {
		error = EAI_FAIL;
		goto out;
	}

	if (!dns_allowed_family(limits, (int)sast.ss_family)) {
		error = NO_RECOVERY;
		goto out;
	}

	flags = (int)nvlist_get_number(nvlin, "flags");

	error = getnameinfo((struct sockaddr *)&sast, salen, host, hostlen,
	    serv, servlen, flags);
	if (error != 0)
		goto out;

	if (host != NULL)
		nvlist_move_string(nvlout, "host", host);
	if (serv != NULL)
		nvlist_move_string(nvlout, "serv", serv);
out:
	if (error != 0) {
		free(host);
		free(serv);
	}
	return (error);
}

static nvlist_t *
addrinfo_pack(const struct addrinfo *ai)
{
	nvlist_t *nvl;

	nvl = nvlist_create(0);
	nvlist_add_number(nvl, "ai_flags", (uint64_t)ai->ai_flags);
	nvlist_add_number(nvl, "ai_family", (uint64_t)ai->ai_family);
	nvlist_add_number(nvl, "ai_socktype", (uint64_t)ai->ai_socktype);
	nvlist_add_number(nvl, "ai_protocol", (uint64_t)ai->ai_protocol);
	nvlist_add_binary(nvl, "ai_addr", ai->ai_addr, (size_t)ai->ai_addrlen);
	if (ai->ai_canonname != NULL)
		nvlist_add_string(nvl, "ai_canonname", ai->ai_canonname);

	return (nvl);
}

static int
dns_getaddrinfo(const nvlist_t *limits, const nvlist_t *nvlin, nvlist_t *nvlout)
{
	struct addrinfo hints, *hintsp, *res, *cur;
	const char *hostname, *servname;
	char nvlname[64];
	nvlist_t *elem;
	unsigned int ii;
	int error, family, n;

	if (!dns_allowed_type(limits, "NAME2ADDR") &&
	    !dns_allowed_type(limits, "NAME"))
		return (NO_RECOVERY);

	hostname = dnvlist_get_string(nvlin, "hostname", NULL);
	servname = dnvlist_get_string(nvlin, "servname", NULL);
	if (nvlist_exists_number(nvlin, "hints.ai_flags")) {
		hints.ai_flags = (int)nvlist_get_number(nvlin,
		    "hints.ai_flags");
		hints.ai_family = (int)nvlist_get_number(nvlin,
		    "hints.ai_family");
		hints.ai_socktype = (int)nvlist_get_number(nvlin,
		    "hints.ai_socktype");
		hints.ai_protocol = (int)nvlist_get_number(nvlin,
		    "hints.ai_protocol");
		hints.ai_addrlen = 0;
		hints.ai_addr = NULL;
		hints.ai_canonname = NULL;
		hints.ai_next = NULL;
		hintsp = &hints;
		family = hints.ai_family;
	} else {
		hintsp = NULL;
		family = AF_UNSPEC;
	}

	if (!dns_allowed_family(limits, family))
		return (NO_RECOVERY);

	error = getaddrinfo(hostname, servname, hintsp, &res);
	if (error != 0)
		goto out;

	for (cur = res, ii = 0; cur != NULL; cur = cur->ai_next, ii++) {
		elem = addrinfo_pack(cur);
		n = snprintf(nvlname, sizeof(nvlname), "res%u", ii);
		assert(n > 0 && n < (int)sizeof(nvlname));
		nvlist_move_nvlist(nvlout, nvlname, elem);
	}

	freeaddrinfo(res);
	error = 0;
out:
	return (error);
}

static bool
limit_has_entry(const nvlist_t *limits, const char *prefix)
{
	const char *name;
	size_t prefixlen;
	void *cookie;

	if (limits == NULL)
		return (false);

	prefixlen = strlen(prefix);

	cookie = NULL;
	while ((name = nvlist_next(limits, NULL, &cookie)) != NULL) {
		if (strncmp(name, prefix, prefixlen) == 0)
			return (true);
	}

	return (false);
}

static int
dns_limit(const nvlist_t *oldlimits, const nvlist_t *newlimits)
{
	const char *name;
	void *cookie;
	int nvtype;
	bool hastype, hasfamily;

	hastype = false;
	hasfamily = false;

	cookie = NULL;
	while ((name = nvlist_next(newlimits, &nvtype, &cookie)) != NULL) {
		if (nvtype == NV_TYPE_STRING) {
			const char *type;

			if (strncmp(name, "type", sizeof("type") - 1) != 0)
				return (EINVAL);
			type = nvlist_get_string(newlimits, name);
			if (strcmp(type, "ADDR2NAME") != 0 &&
			    strcmp(type, "NAME2ADDR") != 0 &&
			    strcmp(type, "ADDR") != 0 &&
			    strcmp(type, "NAME") != 0) {
				return (EINVAL);
			}
			if (!dns_allowed_type(oldlimits, type))
				return (ENOTCAPABLE);
			hastype = true;
		} else if (nvtype == NV_TYPE_NUMBER) {
			int family;

			if (strncmp(name, "family", sizeof("family") - 1) != 0)
				return (EINVAL);
			family = (int)nvlist_get_number(newlimits, name);
			if (!dns_allowed_family(oldlimits, family))
				return (ENOTCAPABLE);
			hasfamily = true;
		} else {
			return (EINVAL);
		}
	}

	/*
	 * If the new limit doesn't mention type or family we have to
	 * check if the current limit does have those. Missing type or
	 * family in the limit means that all types or families are
	 * allowed.
	 */
	if (!hastype) {
		if (limit_has_entry(oldlimits, "type"))
			return (ENOTCAPABLE);
	}
	if (!hasfamily) {
		if (limit_has_entry(oldlimits, "family"))
			return (ENOTCAPABLE);
	}

	return (0);
}

static int
dns_command(const char *cmd, const nvlist_t *limits, nvlist_t *nvlin,
    nvlist_t *nvlout)
{
	int error;

	if (strcmp(cmd, "gethostbyname") == 0)
		error = dns_gethostbyname(limits, nvlin, nvlout);
	else if (strcmp(cmd, "gethostbyaddr") == 0)
		error = dns_gethostbyaddr(limits, nvlin, nvlout);
	else if (strcmp(cmd, "getnameinfo") == 0)
		error = dns_getnameinfo(limits, nvlin, nvlout);
	else if (strcmp(cmd, "getaddrinfo") == 0)
		error = dns_getaddrinfo(limits, nvlin, nvlout);
	else
		error = NO_RECOVERY;

	return (error);
}

CREATE_SERVICE("system.dns", dns_limit, dns_command, 0);
