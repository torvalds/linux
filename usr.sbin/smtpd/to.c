/*	$OpenBSD: to.c,v 1.50 2023/05/31 16:51:46 op Exp $	*/

/*
 * Copyright (c) 2009 Jacek Masiulaniec <jacekm@dobremiasto.net>
 * Copyright (c) 2012 Eric Faurot <eric@openbsd.org>
 * Copyright (c) 2012 Gilles Chehade <gilles@poolp.org>
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

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if IO_TLS
#include <tls.h>
#endif

#include "smtpd.h"
#include "log.h"

static int alias_is_filter(struct expandnode *, const char *, size_t);
static int alias_is_username(struct expandnode *, const char *, size_t);
static int alias_is_address(struct expandnode *, const char *, size_t);
static int alias_is_filename(struct expandnode *, const char *, size_t);
static int alias_is_include(struct expandnode *, const char *, size_t);
static int alias_is_error(struct expandnode *, const char *, size_t);

const char *
sockaddr_to_text(const struct sockaddr *sa)
{
	static char	buf[NI_MAXHOST];

	if (getnameinfo(sa, sa->sa_len, buf, sizeof(buf), NULL, 0,
	    NI_NUMERICHOST))
		return ("(unknown)");
	else
		return (buf);
}

int
text_to_mailaddr(struct mailaddr *maddr, const char *email)
{
	char *username;
	char *hostname;
	char  buffer[LINE_MAX];

	if (strlcpy(buffer, email, sizeof buffer) >= sizeof buffer)
		return 0;

	memset(maddr, 0, sizeof *maddr);

	username = buffer;
	hostname = strrchr(username, '@');

	if (hostname == NULL) {
		if (strlcpy(maddr->user, username, sizeof maddr->user)
		    >= sizeof maddr->user)
			return 0;
	}
	else if (username == hostname) {
		*hostname++ = '\0';
		if (strlcpy(maddr->domain, hostname, sizeof maddr->domain)
		    >= sizeof maddr->domain)
			return 0;
	}
	else {
		*hostname++ = '\0';
		if (strlcpy(maddr->user, username, sizeof maddr->user)
		    >= sizeof maddr->user)
			return 0;
		if (strlcpy(maddr->domain, hostname, sizeof maddr->domain)
		    >= sizeof maddr->domain)
			return 0;
	}

	return 1;
}

const char *
mailaddr_to_text(const struct mailaddr *maddr)
{
	static char  buffer[LINE_MAX];

	(void)strlcpy(buffer, maddr->user, sizeof buffer);
	(void)strlcat(buffer, "@", sizeof buffer);
	if (strlcat(buffer, maddr->domain, sizeof buffer) >= sizeof buffer)
		return NULL;

	return buffer;
}


const char *
sa_to_text(const struct sockaddr *sa)
{
	static char	 buf[NI_MAXHOST + 5];
	char		*p;

	buf[0] = '\0';
	p = buf;

	if (sa->sa_family == AF_LOCAL)
		(void)strlcpy(buf, "local", sizeof buf);
	else if (sa->sa_family == AF_INET) {
		in_addr_t addr;

		addr = ((const struct sockaddr_in *)sa)->sin_addr.s_addr;
		addr = ntohl(addr);
		(void)bsnprintf(p, NI_MAXHOST, "%d.%d.%d.%d",
		    (addr >> 24) & 0xff, (addr >> 16) & 0xff,
		    (addr >> 8) & 0xff, addr & 0xff);
	}
	else if (sa->sa_family == AF_INET6) {
		(void)bsnprintf(p, NI_MAXHOST, "[%s]", sockaddr_to_text(sa));
	}

	return (buf);
}

const char *
ss_to_text(const struct sockaddr_storage *ss)
{
	return (sa_to_text((const struct sockaddr*)ss));
}

const char *
time_to_text(time_t when)
{
	struct tm *lt;
	static char buf[40];
	const char *day[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
	const char *month[] = {"Jan","Feb","Mar","Apr","May","Jun",
			 "Jul","Aug","Sep","Oct","Nov","Dec"};
	const char *tz;
	long offset;

	lt = localtime(&when);
	if (lt == NULL || when == 0)
		fatalx("time_to_text: localtime");

	offset = lt->tm_gmtoff;
	tz = lt->tm_zone;

	/* We do not use strftime because it is subject to locale substitution*/
	if (!bsnprintf(buf, sizeof(buf),
	    "%s, %d %s %d %02d:%02d:%02d %c%02d%02d (%s)",
	    day[lt->tm_wday], lt->tm_mday, month[lt->tm_mon],
	    lt->tm_year + 1900,
	    lt->tm_hour, lt->tm_min, lt->tm_sec,
	    offset >= 0 ? '+' : '-',
	    abs((int)offset / 3600),
	    abs((int)offset % 3600) / 60,
	    tz))
		fatalx("time_to_text: bsnprintf");

	return buf;
}

const char *
duration_to_text(time_t t)
{
	static char	dst[64];
	char		buf[64];
	int		h, m, s;
	long long	d;

	if (t == 0) {
		(void)strlcpy(dst, "0s", sizeof dst);
		return (dst);
	}

	dst[0] = '\0';
	if (t < 0) {
		(void)strlcpy(dst, "-", sizeof dst);
		t = -t;
	}

	s = t % 60;
	t /= 60;
	m = t % 60;
	t /= 60;
	h = t % 24;
	d = t / 24;

	if (d) {
		(void)snprintf(buf, sizeof buf, "%lldd", d);
		(void)strlcat(dst, buf, sizeof dst);
	}
	if (h) {
		(void)snprintf(buf, sizeof buf, "%dh", h);
		(void)strlcat(dst, buf, sizeof dst);
	}
	if (m) {
		(void)snprintf(buf, sizeof buf, "%dm", m);
		(void)strlcat(dst, buf, sizeof dst);
	}
	if (s) {
		(void)snprintf(buf, sizeof buf, "%ds", s);
		(void)strlcat(dst, buf, sizeof dst);
	}

	return (dst);
}

int
text_to_netaddr(struct netaddr *netaddr, const char *s)
{
	struct sockaddr_storage	ss;
	struct sockaddr_in	ssin;
	struct sockaddr_in6	ssin6;
	int			bits;
	char			buf[NI_MAXHOST];
	size_t			len;

	memset(&ssin, 0, sizeof(struct sockaddr_in));
	memset(&ssin6, 0, sizeof(struct sockaddr_in6));

	if (strncasecmp("IPv6:", s, 5) == 0)
		s += 5;

	bits = inet_net_pton(AF_INET, s, &ssin.sin_addr,
	    sizeof(struct in_addr));
	if (bits != -1) {
		ssin.sin_family = AF_INET;
		memcpy(&ss, &ssin, sizeof(ssin));
		ss.ss_len = sizeof(struct sockaddr_in);
	} else {
		if (s[0] != '[') {
			if ((len = strlcpy(buf, s, sizeof buf)) >= sizeof buf)
				return 0;
		}
		else {
			s++;
			if (strncasecmp("IPv6:", s, 5) == 0)
				s += 5;
			if ((len = strlcpy(buf, s, sizeof buf)) >= sizeof buf)
				return 0;
			if (buf[len-1] != ']')
				return 0;
			buf[len-1] = 0;
		}
		bits = inet_net_pton(AF_INET6, buf, &ssin6.sin6_addr,
		    sizeof(struct in6_addr));
		if (bits == -1)
			return 0;
		ssin6.sin6_family = AF_INET6;
		memcpy(&ss, &ssin6, sizeof(ssin6));
		ss.ss_len = sizeof(struct sockaddr_in6);
	}

	netaddr->ss   = ss;
	netaddr->bits = bits;
	return 1;
}

int
text_to_relayhost(struct relayhost *relay, const char *s)
{
	static const struct schema {
		const char	*name;
		int		 tls;
		uint16_t	 flags;
		uint16_t	 port;
	} schemas [] = {
		/*
		 * new schemas should be *appended* otherwise the default
		 * schema index needs to be updated later in this function.
		 */
		{ "smtp://",		RELAY_TLS_OPPORTUNISTIC, 0,		25 },
		{ "smtp+tls://",	RELAY_TLS_STARTTLS,	 0,		25 },
		{ "smtp+notls://",	RELAY_TLS_NO,		 0,		25 },
		/* need to specify an explicit port for LMTP */
		{ "lmtp://",		RELAY_TLS_NO,		 RELAY_LMTP,	0 },
		{ "smtps://",		RELAY_TLS_SMTPS,	 0,		465 }
	};
	const char     *errstr = NULL;
	char	       *p, *q;
	char		buffer[1024];
	char	       *beg, *end;
	size_t		i;
	size_t		len;

	memset(buffer, 0, sizeof buffer);
	if (strlcpy(buffer, s, sizeof buffer) >= sizeof buffer)
		return 0;

	for (i = 0; i < nitems(schemas); ++i)
		if (strncasecmp(schemas[i].name, s,
		    strlen(schemas[i].name)) == 0)
			break;

	if (i == nitems(schemas)) {
		/* there is a schema, but it's not recognized */
		if (strstr(buffer, "://"))
			return 0;

		/* no schema, default to smtp:// */
		i = 0;
		p = buffer;
	}
	else
		p = buffer + strlen(schemas[i].name);

	relay->tls = schemas[i].tls;
	relay->flags = schemas[i].flags;
	relay->port = schemas[i].port;

	/* first, we extract the label if any */
	if ((q = strchr(p, '@')) != NULL) {
		*q = 0;
		if (strlcpy(relay->authlabel, p, sizeof (relay->authlabel))
		    >= sizeof (relay->authlabel))
			return 0;
		p = q + 1;
	}

	/* then, we extract the mail exchanger */
	beg = end = p;
	if (*beg == '[') {
		if ((end = strchr(beg, ']')) == NULL)
			return 0;
		/* skip ']', it has to be included in the relay hostname */
		++end;
		len = end - beg;
	}
	else {
		for (end = beg; *end; ++end)
			if (!isalnum((unsigned char)*end) &&
			    *end != '_' && *end != '.' && *end != '-')
				break;
		len = end - beg;
	}
	if (len >= sizeof relay->hostname)
		return 0;
	for (i = 0; i < len; ++i)
		relay->hostname[i] = beg[i];
	relay->hostname[i] = 0;

	/* finally, we extract the port */
	p = beg + len;
	if (*p == ':') {
		relay->port = strtonum(p+1, 1, IPPORT_HILASTAUTO, &errstr);
		if (errstr)
			return 0;
	}

	if (!valid_domainpart(relay->hostname))
		return 0;
	if ((relay->flags & RELAY_LMTP) && (relay->port == 0))
		return 0;
	if (relay->authlabel[0]) {
		/* disallow auth on non-tls scheme. */
		if (relay->tls != RELAY_TLS_STARTTLS &&
		    relay->tls != RELAY_TLS_SMTPS)
			return 0;
		relay->flags |= RELAY_AUTH;
	}

	return 1;
}

uint64_t
text_to_evpid(const char *s)
{
	uint64_t ulval;
	char	 *ep;

	errno = 0;
	ulval = strtoull(s, &ep, 16);
	if (s[0] == '\0' || *ep != '\0')
		return 0;
	if (errno == ERANGE && ulval == ULLONG_MAX)
		return 0;
	if (ulval == 0)
		return 0;
	return (ulval);
}

uint32_t
text_to_msgid(const char *s)
{
	uint64_t ulval;
	char	 *ep;

	errno = 0;
	ulval = strtoull(s, &ep, 16);
	if (s[0] == '\0' || *ep != '\0')
		return 0;
	if (errno == ERANGE && ulval == ULLONG_MAX)
		return 0;
	if (ulval == 0)
		return 0;
	if (ulval > 0xffffffff)
		return 0;
	return (ulval & 0xffffffff);
}

const char *
rule_to_text(struct rule *r)
{
	static char buf[4096];

	memset(buf, 0, sizeof buf);
	(void)strlcpy(buf, "match", sizeof buf);
	if (r->flag_tag) {
		if (r->flag_tag < 0)
			(void)strlcat(buf, " !", sizeof buf);
		(void)strlcat(buf, " tag ", sizeof buf);
		(void)strlcat(buf, r->table_tag, sizeof buf);
	}

	if (r->flag_from) {
		if (r->flag_from < 0)
			(void)strlcat(buf, " !", sizeof buf);
		if (r->flag_from_socket)
			(void)strlcat(buf, " from socket", sizeof buf);
		else if (r->flag_from_rdns) {
			(void)strlcat(buf, " from rdns", sizeof buf);
			if (r->table_from) {
				(void)strlcat(buf, " ", sizeof buf);
				(void)strlcat(buf, r->table_from, sizeof buf);
			}
		}
		else if (strcmp(r->table_from, "<anyhost>") == 0)
			(void)strlcat(buf, " from any", sizeof buf);
		else if (strcmp(r->table_from, "<localhost>") == 0)
			(void)strlcat(buf, " from local", sizeof buf);
		else {
			(void)strlcat(buf, " from src ", sizeof buf);
			(void)strlcat(buf, r->table_from, sizeof buf);
		}
	}

	if (r->flag_for) {
		if (r->flag_for < 0)
			(void)strlcat(buf, " !", sizeof buf);
		if (strcmp(r->table_for, "<anydestination>") == 0)
			(void)strlcat(buf, " for any", sizeof buf);
		else if (strcmp(r->table_for, "<localnames>") == 0)
			(void)strlcat(buf, " for local", sizeof buf);
		else {
			(void)strlcat(buf, " for domain ", sizeof buf);
			(void)strlcat(buf, r->table_for, sizeof buf);
		}
	}

	if (r->flag_smtp_helo) {
		if (r->flag_smtp_helo < 0)
			(void)strlcat(buf, " !", sizeof buf);
		(void)strlcat(buf, " helo ", sizeof buf);
		(void)strlcat(buf, r->table_smtp_helo, sizeof buf);
	}

	if (r->flag_smtp_auth) {
		if (r->flag_smtp_auth < 0)
			(void)strlcat(buf, " !", sizeof buf);
		(void)strlcat(buf, " auth", sizeof buf);
		if (r->table_smtp_auth) {
			(void)strlcat(buf, " ", sizeof buf);
			(void)strlcat(buf, r->table_smtp_auth, sizeof buf);
		}
	}

	if (r->flag_smtp_starttls) {
		if (r->flag_smtp_starttls < 0)
			(void)strlcat(buf, " !", sizeof buf);
		(void)strlcat(buf, " tls", sizeof buf);
	}

	if (r->flag_smtp_mail_from) {
		if (r->flag_smtp_mail_from < 0)
			(void)strlcat(buf, " !", sizeof buf);
		(void)strlcat(buf, " mail-from ", sizeof buf);
		(void)strlcat(buf, r->table_smtp_mail_from, sizeof buf);
	}

	if (r->flag_smtp_rcpt_to) {
		if (r->flag_smtp_rcpt_to < 0)
			(void)strlcat(buf, " !", sizeof buf);
		(void)strlcat(buf, " rcpt-to ", sizeof buf);
		(void)strlcat(buf, r->table_smtp_rcpt_to, sizeof buf);
	}
	(void)strlcat(buf, " action ", sizeof buf);
	if (r->reject)
		(void)strlcat(buf, "reject", sizeof buf);
	else
		(void)strlcat(buf, r->dispatcher, sizeof buf);
	return buf;
}


int
text_to_userinfo(struct userinfo *userinfo, const char *s)
{
	char		buf[PATH_MAX];
	char	       *p;
	const char     *errstr;

	memset(buf, 0, sizeof buf);
	p = buf;
	while (*s && *s != ':')
		*p++ = *s++;
	if (*s++ != ':')
		goto error;

	if (strlcpy(userinfo->username, buf,
		sizeof userinfo->username) >= sizeof userinfo->username)
		goto error;

	memset(buf, 0, sizeof buf);
	p = buf;
	while (*s && *s != ':')
		*p++ = *s++;
	if (*s++ != ':')
		goto error;
	userinfo->uid = strtonum(buf, 0, UID_MAX, &errstr);
	if (errstr)
		goto error;

	memset(buf, 0, sizeof buf);
	p = buf;
	while (*s && *s != ':')
		*p++ = *s++;
	if (*s++ != ':')
		goto error;
	userinfo->gid = strtonum(buf, 0, GID_MAX, &errstr);
	if (errstr)
		goto error;

	if (strlcpy(userinfo->directory, s,
		sizeof userinfo->directory) >= sizeof userinfo->directory)
		goto error;

	return 1;

error:
	return 0;
}

int
text_to_credentials(struct credentials *creds, const char *s)
{
	char   *p;
	char	buffer[LINE_MAX];
	size_t	offset;

	p = strchr(s, ':');
	if (p == NULL) {
		creds->username[0] = '\0';
		if (strlcpy(creds->password, s, sizeof creds->password)
		    >= sizeof creds->password)
			return 0;
		return 1;
	}

	offset = p - s;

	memset(buffer, 0, sizeof buffer);
	if (strlcpy(buffer, s, sizeof buffer) >= sizeof buffer)
		return 0;
	p = buffer + offset;
	*p = '\0';

	if (strlcpy(creds->username, buffer, sizeof creds->username)
	    >= sizeof creds->username)
		return 0;
	if (strlcpy(creds->password, p+1, sizeof creds->password)
	    >= sizeof creds->password)
		return 0;

	return 1;
}

int
text_to_expandnode(struct expandnode *expandnode, const char *s)
{
	size_t	l;

	l = strlen(s);
	if (alias_is_error(expandnode, s, l) ||
	    alias_is_include(expandnode, s, l) ||
	    alias_is_filter(expandnode, s, l) ||
	    alias_is_filename(expandnode, s, l) ||
	    alias_is_address(expandnode, s, l) ||
	    alias_is_username(expandnode, s, l))
		return (1);

	return (0);
}

const char *
expandnode_to_text(struct expandnode *expandnode)
{
	switch (expandnode->type) {
	case EXPAND_FILTER:
	case EXPAND_FILENAME:
	case EXPAND_INCLUDE:
	case EXPAND_ERROR:
	case EXPAND_USERNAME:
		return expandnode->u.user;
	case EXPAND_ADDRESS:
		return mailaddr_to_text(&expandnode->u.mailaddr);
	case EXPAND_INVALID:
		break;
	}

	return NULL;
}

/******/
static int
alias_is_filter(struct expandnode *alias, const char *line, size_t len)
{
	int	v = 0;

	if (*line == '"')
		v = 1;
	if (*(line+v) == '|') {
		if (strlcpy(alias->u.buffer, line + v + 1,
		    sizeof(alias->u.buffer)) >= sizeof(alias->u.buffer))
			return 0;
		if (v) {
			v = strlen(alias->u.buffer);
			if (v == 0)
				return (0);
			if (alias->u.buffer[v-1] != '"')
				return (0);
			alias->u.buffer[v-1] = '\0';
		}
		alias->type = EXPAND_FILTER;
		return (1);
	}
	return (0);
}

static int
alias_is_username(struct expandnode *alias, const char *line, size_t len)
{
	memset(alias, 0, sizeof *alias);

	if (strlcpy(alias->u.user, line,
	    sizeof(alias->u.user)) >= sizeof(alias->u.user))
		return 0;

	while (*line) {
		if (!isalnum((unsigned char)*line) &&
		    *line != '_' && *line != '.' && *line != '-' && *line != '+')
			return 0;
		++line;
	}

	alias->type = EXPAND_USERNAME;
	return 1;
}

static int
alias_is_address(struct expandnode *alias, const char *line, size_t len)
{
	char *domain;

	memset(alias, 0, sizeof *alias);

	if (len < 3)	/* x@y */
		return 0;

	domain = strchr(line, '@');
	if (domain == NULL)
		return 0;

	/* @ cannot start or end an address */
	if (domain == line || domain == line + len - 1)
		return 0;

	/* scan pre @ for disallowed chars */
	*domain++ = '\0';
	(void)strlcpy(alias->u.mailaddr.user, line, sizeof(alias->u.mailaddr.user));
	(void)strlcpy(alias->u.mailaddr.domain, domain,
	    sizeof(alias->u.mailaddr.domain));

	while (*line) {
		char allowedset[] = "!#$%*/?|^{}`~&'+-=_.";
		if (!isalnum((unsigned char)*line) &&
		    strchr(allowedset, *line) == NULL)
			return 0;
		++line;
	}

	while (*domain) {
		char allowedset[] = "-.";
		if (!isalnum((unsigned char)*domain) &&
		    strchr(allowedset, *domain) == NULL)
			return 0;
		++domain;
	}

	alias->type = EXPAND_ADDRESS;
	return 1;
}

static int
alias_is_filename(struct expandnode *alias, const char *line, size_t len)
{
	memset(alias, 0, sizeof *alias);

	if (*line != '/')
		return 0;

	if (strlcpy(alias->u.buffer, line,
	    sizeof(alias->u.buffer)) >= sizeof(alias->u.buffer))
		return 0;
	alias->type = EXPAND_FILENAME;
	return 1;
}

static int
alias_is_include(struct expandnode *alias, const char *line, size_t len)
{
	size_t skip;

	memset(alias, 0, sizeof *alias);

	if (strncasecmp(":include:", line, 9) == 0)
		skip = 9;
	else if (strncasecmp("include:", line, 8) == 0)
		skip = 8;
	else
		return 0;

	if (!alias_is_filename(alias, line + skip, len - skip))
		return 0;

	alias->type = EXPAND_INCLUDE;
	return 1;
}

static int
alias_is_error(struct expandnode *alias, const char *line, size_t len)
{
	size_t	skip;

	memset(alias, 0, sizeof *alias);

	if (strncasecmp(":error:", line, 7) == 0)
		skip = 7;
	else if (strncasecmp("error:", line, 6) == 0)
		skip = 6;
	else
		return 0;

	if (strlcpy(alias->u.buffer, line + skip,
	    sizeof(alias->u.buffer)) >= sizeof(alias->u.buffer))
		return 0;

	if (strlen(alias->u.buffer) < 5)
		return 0;

	/* [45][0-9]{2} [a-zA-Z0-9].* */
	if (alias->u.buffer[3] != ' ' ||
	    !isalnum((unsigned char)alias->u.buffer[4]) ||
	    (alias->u.buffer[0] != '4' && alias->u.buffer[0] != '5') ||
	    !isdigit((unsigned char)alias->u.buffer[1]) ||
	    !isdigit((unsigned char)alias->u.buffer[2]))
		return 0;

	alias->type = EXPAND_ERROR;
	return 1;
}

#if IO_TLS
const char *
tls_to_text(struct tls *tls)
{
	static char buf[256];

	(void)snprintf(buf, sizeof buf, "%s:%s:%d",
	    tls_conn_version(tls),
	    tls_conn_cipher(tls),
	    tls_conn_cipher_strength(tls));

	return (buf);
}
#endif
