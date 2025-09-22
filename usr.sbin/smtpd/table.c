/*	$OpenBSD: table.c,v 1.54 2024/06/09 10:13:05 gilles Exp $	*/

/*
 * Copyright (c) 2013 Eric Faurot <eric@openbsd.org>
 * Copyright (c) 2008 Gilles Chehade <gilles@poolp.org>
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

#include <sys/stat.h>

#include <net/if.h>

#include <arpa/inet.h>
#include <errno.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>

#include "smtpd.h"
#include "log.h"

struct table_backend *table_backend_lookup(const char *);

extern struct table_backend table_backend_static;
extern struct table_backend table_backend_db;
extern struct table_backend table_backend_getpwnam;
extern struct table_backend table_backend_proc;

static int table_parse_lookup(enum table_service, const char *, const char *,
    union lookup *);
static int parse_sockaddr(struct sockaddr *, int, const char *);

static unsigned int last_table_id = 0;

static struct table_backend *backends[] = {
	&table_backend_static,
	&table_backend_db,
	&table_backend_getpwnam,
	&table_backend_proc,
	NULL
};

struct table_backend *
table_backend_lookup(const char *backend)
{
	int i;

	if (!strcmp(backend, "file"))
		backend = "static";

	for (i = 0; backends[i]; i++)
		if (!strcmp(backends[i]->name, backend))
			return (backends[i]);

	return NULL;
}

const char *
table_service_name(enum table_service s)
{
	switch (s) {
	case K_NONE:		return "none";
	case K_ALIAS:		return "alias";
	case K_DOMAIN:		return "domain";
	case K_CREDENTIALS:	return "credentials";
	case K_NETADDR:		return "netaddr";
	case K_USERINFO:	return "userinfo";
	case K_SOURCE:		return "source";
	case K_MAILADDR:	return "mailaddr";
	case K_ADDRNAME:	return "addrname";
	case K_MAILADDRMAP:	return "mailaddrmap";
	case K_RELAYHOST:	return "relayhost";
	case K_STRING:		return "string";
	case K_REGEX:		return "regex";
	case K_AUTH:		return "auth";
	}
	return "???";
}

int
table_service_from_name(const char *service)
{
	if (!strcmp(service, "none"))
		return K_NONE;
	if (!strcmp(service, "alias"))
		return K_ALIAS;
	if (!strcmp(service, "domain"))
		return K_DOMAIN;
	if (!strcmp(service, "credentials"))
		return K_CREDENTIALS;
	if (!strcmp(service, "netaddr"))
		return K_NETADDR;
	if (!strcmp(service, "userinfo"))
		return K_USERINFO;
	if (!strcmp(service, "source"))
		return K_SOURCE;
	if (!strcmp(service, "mailaddr"))
		return K_MAILADDR;
	if (!strcmp(service, "addrname"))
		return K_ADDRNAME;
	if (!strcmp(service, "mailaddrmap"))
		return K_MAILADDRMAP;
	if (!strcmp(service, "relayhost"))
		return K_RELAYHOST;
	if (!strcmp(service, "string"))
		return K_STRING;
	if (!strcmp(service, "regex"))
		return K_REGEX;
	if (!strcmp(service, "auth"))
		return K_AUTH;
	return (-1);
}

struct table *
table_find(struct smtpd *conf, const char *name)
{
	return dict_get(conf->sc_tables_dict, name);
}

int
table_match(struct table *table, enum table_service kind, const char *key)
{
	return table_lookup(table, kind, key, NULL);
}

int
table_lookup(struct table *table, enum table_service kind, const char *key,
    union lookup *lk)
{
	char lkey[1024], *buf = NULL;
	int r;

	r = -1;
	if (table->t_backend->lookup == NULL)
		errno = ENOTSUP;
	else if (!lowercase(lkey, key, sizeof lkey)) {
		log_warnx("warn: lookup key too long: %s", key);
		errno = EINVAL;
	}
	else
		r = table->t_backend->lookup(table, kind, lkey, lk ? &buf : NULL);

	if (r == 1) {
		log_trace(TRACE_LOOKUP, "lookup: %s \"%s\" as %s in table %s:%s -> %s%s%s",
		    lk ? "lookup" : "match",
		    key,
		    table_service_name(kind),
		    table->t_backend->name,
		    table->t_name,
		    lk ? "\"" : "",
		    lk ? buf : "true",
		    lk ? "\"" : "");
		if (buf)
			r = table_parse_lookup(kind, lkey, buf, lk);
	}
	else
		log_trace(TRACE_LOOKUP, "lookup: %s \"%s\" as %s in table %s:%s -> %s%s",
		    lk ? "lookup" : "match",
		    key,
		    table_service_name(kind),
		    table->t_backend->name,
		    table->t_name,
		    (r == -1) ? "error: " : (lk ? "none" : "false"),
		    (r == -1) ? strerror(errno) : "");

	free(buf);

	return (r);
}

int
table_fetch(struct table *table, enum table_service kind, union lookup *lk)
{
	char *buf = NULL;
	int r;

	r = -1;
	if (table->t_backend->fetch == NULL)
		errno = ENOTSUP;
	else
		r = table->t_backend->fetch(table, kind, &buf);

	if (r == 1) {
		log_trace(TRACE_LOOKUP, "lookup: fetch %s from table %s:%s -> \"%s\"",
		    table_service_name(kind),
		    table->t_backend->name,
		    table->t_name,
		    buf);
		r = table_parse_lookup(kind, NULL, buf, lk);
	}
	else
		log_trace(TRACE_LOOKUP, "lookup: fetch %s from table %s:%s -> %s%s",
		    table_service_name(kind),
		    table->t_backend->name,
		    table->t_name,
		    (r == -1) ? "error: " : "none",
		    (r == -1) ? strerror(errno) : "");

	free(buf);

	return (r);
}

struct table *
table_create(struct smtpd *conf, const char *backend, const char *name,
    const char *config)
{
	struct table		*t;
	struct table_backend	*tb;
	char			 path[LINE_MAX];
	size_t			 n;
	struct stat		 sb;

	if (name && table_find(conf, name))
		fatalx("table_create: table \"%s\" already defined", name);

	if ((tb = table_backend_lookup(backend)) == NULL) {
		if ((size_t)snprintf(path, sizeof(path), PATH_LIBEXEC"/table-%s",
			backend) >= sizeof(path)) {
			fatalx("table_create: path too long \""
			    PATH_LIBEXEC"/table-%s\"", backend);
		}
		if (stat(path, &sb) == 0) {
			tb = table_backend_lookup("proc");
			(void)strlcpy(path, backend, sizeof(path));
			if (config) {
				(void)strlcat(path, ":", sizeof(path));
				if (strlcat(path, config, sizeof(path))
				    >= sizeof(path))
					fatalx("table_create: config file path too long");
			}
			config = path;
		}
	}

	if (tb == NULL)
		fatalx("table_create: backend \"%s\" does not exist", backend);

	t = xcalloc(1, sizeof(*t));
	t->t_services = tb->services;
	t->t_backend = tb;

	if (config) {
		if (strlcpy(t->t_config, config, sizeof t->t_config)
		    >= sizeof t->t_config)
			fatalx("table_create: table config \"%s\" too large",
			    t->t_config);
	}

	if (strcmp(tb->name, "static") != 0)
		t->t_type = T_DYNAMIC;

	if (name == NULL)
		(void)snprintf(t->t_name, sizeof(t->t_name), "<dynamic:%u>",
		    last_table_id++);
	else {
		n = strlcpy(t->t_name, name, sizeof(t->t_name));
		if (n >= sizeof(t->t_name))
			fatalx("table_create: table name too long");
	}

	dict_set(conf->sc_tables_dict, t->t_name, t);

	return (t);
}

void
table_destroy(struct smtpd *conf, struct table *t)
{
	dict_xpop(conf->sc_tables_dict, t->t_name);
	free(t);
}

int
table_config(struct table *t)
{
	if (t->t_backend->config == NULL)
		return (1);
	return (t->t_backend->config(t));
}

void
table_add(struct table *t, const char *key, const char *val)
{
	if (t->t_backend->add == NULL)
		fatalx("table_add: cannot add to table");

	if (t->t_backend->add(t, key, val) == 0)
		log_warnx("warn: failed to add \"%s\" in table \"%s\"", key, t->t_name);
}

void
table_dump(struct table *t)
{
	const char *type;
	char buf[LINE_MAX];

	switch(t->t_type) {
	case T_NONE:
		type = "NONE";
		break;
	case T_DYNAMIC:
		type = "DYNAMIC";
		break;
	case T_LIST:
		type = "LIST";
		break;
	case T_HASH:
		type = "HASH";
		break;
	default:
		type = "???";
		break;
	}

	if (t->t_config[0])
		snprintf(buf, sizeof(buf), " config=\"%s\"", t->t_config);
	else
		buf[0] = '\0';

	log_debug("TABLE \"%s\" backend=%s type=%s%s", t->t_name,
	    t->t_backend->name, type, buf);

	if (t->t_backend->dump)
		t->t_backend->dump(t);
}

int
table_check_type(struct table *t, uint32_t mask)
{
	return t->t_type & mask;
}

int
table_check_service(struct table *t, uint32_t mask)
{
	return t->t_services & mask;
}

int
table_check_use(struct table *t, uint32_t tmask, uint32_t smask)
{
	return table_check_type(t, tmask) && table_check_service(t, smask);
}

int
table_open(struct table *t)
{
	if (t->t_backend->open == NULL)
		return (1);
	return (t->t_backend->open(t));
}

void
table_close(struct table *t)
{
	if (t->t_backend->close)
		t->t_backend->close(t);
}

int
table_update(struct table *t)
{
	if (t->t_backend->update == NULL)
		return (1);
	return (t->t_backend->update(t));
}


/*
 * quick reminder:
 * in *_match() s1 comes from session, s2 comes from table
 */

int
table_domain_match(const char *s1, const char *s2)
{
	return hostname_match(s1, s2);
}

int
table_mailaddr_match(const char *s1, const char *s2)
{
	struct mailaddr m1;
	struct mailaddr m2;

	if (!text_to_mailaddr(&m1, s1))
		return 0;
	if (!text_to_mailaddr(&m2, s2))
		return 0;
	return mailaddr_match(&m1, &m2);
}

static int table_match_mask(struct sockaddr_storage *, struct netaddr *);
static int table_inet4_match(struct sockaddr_in *, struct netaddr *);
static int table_inet6_match(struct sockaddr_in6 *, struct netaddr *);

int
table_netaddr_match(const char *s1, const char *s2)
{
	struct netaddr n1;
	struct netaddr n2;

	if (strcasecmp(s1, s2) == 0)
		return 1;
	if (!text_to_netaddr(&n1, s1))
		return 0;
	if (!text_to_netaddr(&n2, s2))
		return 0;
	if (n1.ss.ss_family != n2.ss.ss_family)
		return 0;
	if (n1.ss.ss_len != n2.ss.ss_len)
		return 0;
	return table_match_mask(&n1.ss, &n2);
}

static int
table_match_mask(struct sockaddr_storage *ss, struct netaddr *ssmask)
{
	if (ss->ss_family == AF_INET)
		return table_inet4_match((struct sockaddr_in *)ss, ssmask);

	if (ss->ss_family == AF_INET6)
		return table_inet6_match((struct sockaddr_in6 *)ss, ssmask);

	return (0);
}

static int
table_inet4_match(struct sockaddr_in *ss, struct netaddr *ssmask)
{
	in_addr_t mask;
	int i;

	/* a.b.c.d/8 -> htonl(0xff000000) */
	mask = 0;
	for (i = 0; i < ssmask->bits; ++i)
		mask = (mask >> 1) | 0x80000000;
	mask = htonl(mask);

	/* (addr & mask) == (net & mask) */
	if ((ss->sin_addr.s_addr & mask) ==
	    (((struct sockaddr_in *)ssmask)->sin_addr.s_addr & mask))
		return 1;

	return 0;
}

static int
table_inet6_match(struct sockaddr_in6 *ss, struct netaddr *ssmask)
{
	struct in6_addr	*in;
	struct in6_addr	*inmask;
	struct in6_addr	 mask;
	int		 i;

	memset(&mask, 0, sizeof(mask));
	for (i = 0; i < ssmask->bits / 8; i++)
		mask.s6_addr[i] = 0xff;
	i = ssmask->bits % 8;
	if (i)
		mask.s6_addr[ssmask->bits / 8] = 0xff00 >> i;

	in = &ss->sin6_addr;
	inmask = &((struct sockaddr_in6 *)&ssmask->ss)->sin6_addr;

	for (i = 0; i < 16; i++) {
		if ((in->s6_addr[i] & mask.s6_addr[i]) !=
		    (inmask->s6_addr[i] & mask.s6_addr[i]))
			return (0);
	}

	return (1);
}

int
table_regex_match(const char *string, const char *pattern)
{
	regex_t preg;
	int	cflags = REG_EXTENDED|REG_NOSUB;
	int ret;

	if (strncmp(pattern, "(?i)", 4) == 0) {
		cflags |= REG_ICASE;
		pattern += 4;
	}

	if (regcomp(&preg, pattern, cflags) != 0)
		return (0);

	ret = regexec(&preg, string, 0, NULL, 0);

	regfree(&preg);

	if (ret != 0)
		return (0);

	return (1);
}

void
table_dump_all(struct smtpd *conf)
{
	struct table	*t;
	void		*iter;

	iter = NULL;
	while (dict_iter(conf->sc_tables_dict, &iter, NULL, (void **)&t))
		table_dump(t);
}

void
table_open_all(struct smtpd *conf)
{
	struct table	*t;
	void		*iter;

	iter = NULL;
	while (dict_iter(conf->sc_tables_dict, &iter, NULL, (void **)&t))
		if (!table_open(t))
			fatalx("failed to open table %s", t->t_name);
}

void
table_close_all(struct smtpd *conf)
{
	struct table	*t;
	void		*iter;

	iter = NULL;
	while (dict_iter(conf->sc_tables_dict, &iter, NULL, (void **)&t))
		table_close(t);
}

static int
table_parse_lookup(enum table_service service, const char *key,
    const char *line, union lookup *lk)
{
	char	buffer[LINE_MAX], *p;
	size_t	len;

	len = strlen(line);

	switch (service) {
	case K_ALIAS:
		lk->expand = calloc(1, sizeof(*lk->expand));
		if (lk->expand == NULL)
			return (-1);
		if (!expand_line(lk->expand, line, 1)) {
			expand_free(lk->expand);
			return (-1);
		}
		return (1);

	case K_DOMAIN:
		if (strlcpy(lk->domain.name, line, sizeof(lk->domain.name))
		    >= sizeof(lk->domain.name))
			return (-1);
		return (1);

	case K_CREDENTIALS:

		/* credentials are stored as user:password */
		if (len < 3)
			return (-1);

		/* too big to fit in a smtp session line */
		if (len >= LINE_MAX)
			return (-1);

		p = strchr(line, ':');
		if (p == NULL) {
			if (strlcpy(lk->creds.username, key, sizeof (lk->creds.username))
			    >= sizeof (lk->creds.username))
				return (-1);
			if (strlcpy(lk->creds.password, line, sizeof(lk->creds.password))
			    >= sizeof(lk->creds.password))
				return (-1);
			return (1);
		}

		if (p == line || p == line + len - 1)
			return (-1);

		memmove(lk->creds.username, line, p - line);
		lk->creds.username[p - line] = '\0';

		if (strlcpy(lk->creds.password, p+1, sizeof(lk->creds.password))
		    >= sizeof(lk->creds.password))
			return (-1);

		return (1);

	case K_NETADDR:
		if (!text_to_netaddr(&lk->netaddr, line))
			return (-1);
		return (1);

	case K_USERINFO:
		if (!bsnprintf(buffer, sizeof(buffer), "%s:%s", key, line))
			return (-1);
		if (!text_to_userinfo(&lk->userinfo, buffer))
			return (-1);
 		return (1);

	case K_SOURCE:
		if (parse_sockaddr((struct sockaddr *)&lk->source.addr,
		    PF_UNSPEC, line) == -1)
			return (-1);
		return (1);

	case K_MAILADDR:
		if (!text_to_mailaddr(&lk->mailaddr, line))
			return (-1);
		return (1);

	case K_MAILADDRMAP:
		lk->maddrmap = calloc(1, sizeof(*lk->maddrmap));
		if (lk->maddrmap == NULL)
			return (-1);
		maddrmap_init(lk->maddrmap);
		if (!mailaddr_line(lk->maddrmap, line)) {
			maddrmap_free(lk->maddrmap);
			return (-1);
		}
		return (1);

	case K_ADDRNAME:
		if (parse_sockaddr((struct sockaddr *)&lk->addrname.addr,
		    PF_UNSPEC, key) == -1)
			return (-1);
		if (strlcpy(lk->addrname.name, line, sizeof(lk->addrname.name))
		    >= sizeof(lk->addrname.name))
			return (-1);
		return (1);

	case K_RELAYHOST:
		if (strlcpy(lk->relayhost, line, sizeof(lk->relayhost))
		    >= sizeof(lk->relayhost))
			return (-1);
		return (1);

	default:
		return (-1);
	}
}

static int
parse_sockaddr(struct sockaddr *sa, int family, const char *str)
{
	struct in_addr		 ina;
	struct in6_addr		 in6a;
	struct sockaddr_in	*sin;
	struct sockaddr_in6	*sin6;
	char			*cp;
	char			 addr[NI_MAXHOST];
	const char		*errstr;

	switch (family) {
	case PF_UNSPEC:
		if (parse_sockaddr(sa, PF_INET, str) == 0)
			return (0);
		return parse_sockaddr(sa, PF_INET6, str);

	case PF_INET:
		if (inet_pton(PF_INET, str, &ina) != 1)
			return (-1);

		sin = (struct sockaddr_in *)sa;
		memset(sin, 0, sizeof *sin);
		sin->sin_len = sizeof(struct sockaddr_in);
		sin->sin_family = PF_INET;
		sin->sin_addr.s_addr = ina.s_addr;
		return (0);

	case PF_INET6:
		if (*str == '[')
			str++;
		if (!strncasecmp("ipv6:", str, 5))
			str += 5;

		if (strlcpy(addr, str, sizeof(addr)) >= sizeof(addr))
			return (-1);
		if ((cp = strchr(addr, ']')) != NULL)
			*cp = '\0';
		if ((cp = strchr(addr, SCOPE_DELIMITER)) != NULL)
			*cp++ = '\0';

		if (inet_pton(PF_INET6, addr, &in6a) != 1)
			return (-1);

		sin6 = (struct sockaddr_in6 *)sa;
		memset(sin6, 0, sizeof *sin6);
		sin6->sin6_len = sizeof(struct sockaddr_in6);
		sin6->sin6_family = PF_INET6;
		sin6->sin6_addr = in6a;

		if (cp == NULL)
			return (0);

		if (IN6_IS_ADDR_LINKLOCAL(&in6a) ||
		    IN6_IS_ADDR_MC_LINKLOCAL(&in6a) ||
		    IN6_IS_ADDR_MC_INTFACELOCAL(&in6a))
			if ((sin6->sin6_scope_id = if_nametoindex(cp)))
				return (0);

		sin6->sin6_scope_id = strtonum(cp, 0, UINT32_MAX, &errstr);
		if (errstr)
			return (-1);
		return (0);

	default:
		break;
	}

	return (-1);
}
