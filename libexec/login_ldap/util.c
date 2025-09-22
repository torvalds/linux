/*
 * $OpenBSD: util.c,v 1.5 2023/04/19 12:34:23 jsg Exp $
 * Copyright (c) 2002 Institute for Open Systems Technology Australia (IFOST)
 * Copyright (c) 2007 Michael Erdely <merdely@openbsd.org>
 * Copyright (c) 2019 Martijn van Duren <martijn@openbsd.org>
 *
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <netinet/in.h>

#include <ctype.h>
#include <grp.h>
#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <syslog.h>
#include <tls.h>
#include <netdb.h>
#include <login_cap.h>

#include "aldap.h"
#include "login_ldap.h"

int debug = 0;

static int getscope(char *);

void
dlog(int d, char *fmt, ...)
{
	va_list ap;

	/*
	 * if debugging is on, print everything to stderr
	 * otherwise, syslog it if d = 0. messing with
	 * newlines means there wont be newlines in stuff
	 * that goes to syslog.
	 */

	va_start(ap, fmt);
	if (debug) {
		vfprintf(stderr, fmt, ap);
		fputc('\n', stderr);
	} else if (d == 0)
		vsyslog(LOG_WARNING, fmt, ap);

	va_end(ap);
}

const char *
ldap_resultcode(enum result_code code)
{
#define CODE(_X)	case _X:return (#_X)
	switch (code) {
	CODE(LDAP_SUCCESS);
	CODE(LDAP_OPERATIONS_ERROR);
	CODE(LDAP_PROTOCOL_ERROR);
	CODE(LDAP_TIMELIMIT_EXCEEDED);
	CODE(LDAP_SIZELIMIT_EXCEEDED);
	CODE(LDAP_COMPARE_FALSE);
	CODE(LDAP_COMPARE_TRUE);
	CODE(LDAP_STRONG_AUTH_NOT_SUPPORTED);
	CODE(LDAP_STRONG_AUTH_REQUIRED);
	CODE(LDAP_REFERRAL);
	CODE(LDAP_ADMINLIMIT_EXCEEDED);
	CODE(LDAP_UNAVAILABLE_CRITICAL_EXTENSION);
	CODE(LDAP_CONFIDENTIALITY_REQUIRED);
	CODE(LDAP_SASL_BIND_IN_PROGRESS);
	CODE(LDAP_NO_SUCH_ATTRIBUTE);
	CODE(LDAP_UNDEFINED_TYPE);
	CODE(LDAP_INAPPROPRIATE_MATCHING);
	CODE(LDAP_CONSTRAINT_VIOLATION);
	CODE(LDAP_TYPE_OR_VALUE_EXISTS);
	CODE(LDAP_INVALID_SYNTAX);
	CODE(LDAP_NO_SUCH_OBJECT);
	CODE(LDAP_ALIAS_PROBLEM);
	CODE(LDAP_INVALID_DN_SYNTAX);
	CODE(LDAP_ALIAS_DEREF_PROBLEM);
	CODE(LDAP_INAPPROPRIATE_AUTH);
	CODE(LDAP_INVALID_CREDENTIALS);
	CODE(LDAP_INSUFFICIENT_ACCESS);
	CODE(LDAP_BUSY);
	CODE(LDAP_UNAVAILABLE);
	CODE(LDAP_UNWILLING_TO_PERFORM);
	CODE(LDAP_LOOP_DETECT);
	CODE(LDAP_NAMING_VIOLATION);
	CODE(LDAP_OBJECT_CLASS_VIOLATION);
	CODE(LDAP_NOT_ALLOWED_ON_NONLEAF);
	CODE(LDAP_NOT_ALLOWED_ON_RDN);
	CODE(LDAP_ALREADY_EXISTS);
	CODE(LDAP_NO_OBJECT_CLASS_MODS);
	CODE(LDAP_AFFECTS_MULTIPLE_DSAS);
	CODE(LDAP_OTHER);
	}

	return ("UNKNOWN_ERROR");
};


static int
parse_server_line(char *buf, struct aldap_url *s)
{
	/**
	 * host=[<protocol>://]<hostname>[:port]
	 *
	 * must have a hostname
	 * protocol can be "ldap", "ldaps", "ldap+tls" or "ldapi"
	 * for ldap and ldap+tls, port defaults to 389
	 * for ldaps, port defaults to 636
	 */

	if (buf == NULL) {
		dlog(1, "%s got NULL buf!", __func__);
		return 0;
	}

	dlog(1, "parse_server_line buf = %s", buf);

	memset(s, 0, sizeof(*s));

	if (aldap_parse_url(buf, s) == -1) {
		dlog(0, "failed to parse host %s", buf);
		return 0;
	}

	if (s->protocol == -1)
		s->protocol = LDAP;
	if (s->protocol != LDAPI && s->port == 0) {
		if (s->protocol == LDAPS)
			s->port = 636;
		else
			s->port = 389;
	}

	return 1;
}

int
parse_conf(struct auth_ctx *ctx, const char *path)
{
	FILE *cf;
	struct stat sb;
	struct group *grp;
	struct aldap_urlq *url;
	char *buf = NULL, *key, *value, *tail;
	const char *errstr;
	size_t buflen = 0;
	ssize_t linelen;

	dlog(1, "Parsing config file '%s'", path);

	if ((cf = fopen(path, "r")) == NULL) {
		dlog(0, "Can't open config file: %s", strerror(errno));
		return 0;
	}
	if (fstat(fileno(cf), &sb) == -1) {
		dlog(0, "Can't stat config file: %s", strerror(errno));
		return 0;
	}
	if ((grp = getgrnam("auth")) == NULL) {
		dlog(0, "Can't find group auth");
		return 0;
	}
	if (sb.st_uid != 0 ||
	    sb.st_gid != grp->gr_gid ||
	    (sb.st_mode & S_IRWXU) != (S_IRUSR | S_IWUSR) ||
	    (sb.st_mode & S_IRWXG) != S_IRGRP ||
	    (sb.st_mode & S_IRWXO) != 0) {
		dlog(0, "Wrong permissions for config file");
		return 0;
	}

	/* We need a default scope */
	ctx->gscope = ctx->scope = getscope(NULL);

	while ((linelen = getline(&buf, &buflen, cf)) != -1) {
		if (buf[linelen - 1] == '\n')
			buf[linelen -1] = '\0';
		/* Allow leading spaces */
		for (key = buf; key[0] != '\0' && isspace(key[0]); key++)
			continue;
		/* Comment or white lines */
		if (key[0] == '#' || key[0] == '\0')
			continue;
		if ((tail = value = strchr(key, '=')) == NULL) {
			dlog(0, "Missing value for option '%s'", key);
			return 0;
		}
		value++;
		/* Don't fail over trailing key spaces */
		for (tail--; isspace(tail[0]); tail--)
			continue;
		tail[1] = '\0';
		if (strcmp(key, "host") == 0) {
			if ((url = calloc(1, sizeof(*url))) == NULL) {
				dlog(0, "Failed to add %s: %s", value,
				    strerror(errno));
				continue;
			}
			if (parse_server_line(value, &(url->s)) == 0) {
				free(url);
				return 0;
			}
			TAILQ_INSERT_TAIL(&(ctx->s), url, entries);
		} else if (strcmp(key, "basedn") == 0) {
			free(ctx->basedn);
			if ((ctx->basedn = strdup(value)) == NULL) {
				dlog(0, "%s", strerror(errno));
				return 0;
			}
		} else if (strcmp(key, "binddn") == 0) {
			free(ctx->binddn);
			if ((ctx->binddn = parse_filter(ctx, value)) == NULL)
				return 0;
		} else if (strcmp(key, "bindpw") == 0) {
			free(ctx->bindpw);
			if ((ctx->bindpw = strdup(value)) == NULL) {
				dlog(0, "%s", strerror(errno));
				return 0;
			}
		} else if (strcmp(key, "timeout") == 0) {
			ctx->timeout = strtonum(value, 0, INT_MAX, &errstr);
			if (ctx->timeout == 0 && errstr != NULL) {
				dlog(0, "timeout %s", errstr);
				return 0;
			}
		} else if (strcmp(key, "filter") == 0) {
			free(ctx->filter);
			if ((ctx->filter = parse_filter(ctx, value)) == NULL)
				return 0;
		} else if (strcmp(key, "scope") == 0) {
			if ((ctx->scope = getscope(value)) == -1)
				return 0;
		} else if (strcmp(key, "cacert") == 0) {
			free(ctx->cacert);
			if ((ctx->cacert = strdup(value)) == NULL) {
				dlog(0, "%s", strerror(errno));
				return 0;
			}
		} else if (strcmp(key, "cacertdir") == 0) {
			free(ctx->cacertdir);
			if ((ctx->cacertdir = strdup(value)) == NULL) {
				dlog(0, "%s", strerror(errno));
				return 0;
			}
		} else if (strcmp(key, "gbasedn") == 0) {
			free(ctx->gbasedn);
			if ((ctx->gbasedn = strdup(value)) == NULL) {
				dlog(0, "%s", strerror(errno));
				return 0;
			}
		} else if (strcmp(key, "gfilter") == 0) {
			free(ctx->gfilter);
			if ((ctx->gfilter = strdup(value)) == NULL) {
				dlog(0, "%s", strerror(errno));
				return 0;
			}
		} else if (strcmp(key, "gscope") == 0) {
			if ((ctx->scope = getscope(value)) == -1)
				return 0;
		} else {
			dlog(0, "Unknown option '%s'", key);
			return 0;
		}
	}
	if (ferror(cf)) {
		dlog(0, "Can't read config file: %s", strerror(errno));
		return 0;
	}
	if (TAILQ_EMPTY(&(ctx->s))) {
		dlog(0, "Missing host");
		return 0;
	}
	if (ctx->basedn == NULL && ctx->binddn == NULL) {
		dlog(0, "Missing basedn or binddn");
		return 0;
	}
	return 1;
}

int
do_conn(struct auth_ctx *ctx, struct aldap_url *url)
{
	struct addrinfo		 ai, *res, *res0;
	struct sockaddr_un	 un;
	struct aldap_message	*m;
	struct tls_config	*tls_config;
	const char		*errstr;
	char			 port[6];
	int			 fd, code;

	dlog(1, "host %s, port %d", url->host, url->port);

	if (url->protocol == LDAPI) {
		memset(&un, 0, sizeof(un));
		un.sun_family = AF_UNIX;
		if (strlcpy(un.sun_path, url->host,
		    sizeof(un.sun_path)) >= sizeof(un.sun_path)) {
			dlog(0, "socket '%s' too long", url->host);
			return 0;
		}
		if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1 ||
		    connect(fd, (struct sockaddr *)&un, sizeof(un)) == -1) {
			dlog(0, "can't create socket '%s'", url->host);
			return 0;
		}
	} else {
		memset(&ai, 0, sizeof(ai));
		ai.ai_family = AF_UNSPEC;
		ai.ai_socktype = SOCK_STREAM;
		ai.ai_protocol = IPPROTO_TCP;
		(void)snprintf(port, sizeof(port), "%u", url->port);
		if ((code = getaddrinfo(url->host, port,
		    &ai, &res0)) != 0) {
			dlog(0, "%s", gai_strerror(code));
			return 0;
		}
		for (res = res0; res; res = res->ai_next, fd = -1) {
			if ((fd = socket(res->ai_family, res->ai_socktype,
			    res->ai_protocol)) == -1)
				continue;

			if (connect(fd, res->ai_addr, res->ai_addrlen) >= 0)
				break;

			close(fd);
		}
		freeaddrinfo(res0);
		if (fd == -1)
			return 0;
	}

	ctx->ld = aldap_init(fd);
	if (ctx->ld == NULL) {
		dlog(0, "aldap_open(%s:%hd) failed", url->host, url->port);
		return 0;
	}

	dlog(1, "connect success!");

	if (url->protocol == LDAPTLS) {
		dlog(1, "starttls!");
		if (aldap_req_starttls(ctx->ld) == -1) {
			dlog(0, "failed to request STARTTLS");
			goto fail;
		}

		if ((m = aldap_parse(ctx->ld)) == NULL) {
			dlog(0, "failed to parse STARTTLS response");
			goto fail;
		}

		if (ctx->ld->msgid != m->msgid ||
		    (code = aldap_get_resultcode(m)) != LDAP_SUCCESS) {
			dlog(0, "STARTTLS failed: %s(%d)",
			    ldap_resultcode(code), code);
			aldap_freemsg(m);
			goto fail;
		}
		aldap_freemsg(m);
	}
	if (url->protocol == LDAPTLS || url->protocol == LDAPS) {
		dlog(1, "%s: starting TLS", __func__);

		if ((tls_config = tls_config_new()) == NULL) {
			dlog(0, "TLS config failed");
			goto fail;
		}

		if (ctx->cacert != NULL &&
		    tls_config_set_ca_file(tls_config, ctx->cacert) == -1) {
			dlog(0, "Failed to set ca file %s", ctx->cacert);
			goto fail;
		}
		if (ctx->cacertdir != NULL &&
		    tls_config_set_ca_path(tls_config, ctx->cacertdir) == -1) {
			dlog(0, "Failed to set ca dir %s", ctx->cacertdir);
			goto fail;
		}

		if (aldap_tls(ctx->ld, tls_config, url->host) < 0) {
			aldap_get_errno(ctx->ld, &errstr);
			dlog(0, "TLS failed: %s", errstr);
			goto fail;
		}
	}
	return 1;
fail:
	aldap_close(ctx->ld);
	return 0;
}

int
conn(struct auth_ctx *ctx)
{
	struct aldap_urlq *url;

	TAILQ_FOREACH(url, &(ctx->s), entries) {
		if (do_conn(ctx, &(url->s)))
			return 1;
	}

	/* all the urls have failed */
	return 0;
}

static int
getscope(char *scope)
{
	if (scope == NULL || scope[0] == '\0')
		return LDAP_SCOPE_SUBTREE;

	if (strcmp(scope, "base") == 0)
		return LDAP_SCOPE_BASE;
	else if (strcmp(scope, "one") == 0)
		return LDAP_SCOPE_ONELEVEL;
	else if (strcmp(scope, "sub") == 0)
		return LDAP_SCOPE_SUBTREE;

	dlog(0, "Invalid scope");
	return -1;
}

/*
 * Convert format specifiers from the filter in login.conf to their
 * real values. return the new filter in the filter argument.
 */
char *
parse_filter(struct auth_ctx *ctx, const char *str)
{
	char tmp[PATH_MAX];
	char hostname[HOST_NAME_MAX+1];
	const char *p;
	char *q;

	if (str == NULL)
		return NULL;

	/*
	 * copy over from str to q, if we hit a %, substitute the real value,
	 * if we hit a NULL, its the end of the filter string
	 */
	for (p = str, q = tmp; p[0] != '\0' &&
	    ((size_t)(q - tmp) < sizeof(tmp)); p++) {
		if (p[0] == '%') {
			p++;

			/* Make sure we can find the end of tmp for strlcat */
			q[0] = '\0';

			/*
			 * Don't need to check strcat for truncation, since we
			 * will bail on the next iteration
			 */
			switch (p[0]) {
			case 'u': /* username */
				q = tmp + strlcat(tmp, ctx->user, sizeof(tmp));
				break;
			case 'h': /* hostname */
				if (gethostname(hostname, sizeof(hostname)) ==
				    -1) {
					dlog(0, "couldn't get host name for "
					    "%%h %s", strerror(errno));
					return NULL;
				}
				q = tmp + strlcat(tmp, hostname, sizeof(tmp));
				break;
			case 'd': /* user dn */
				if (ctx->userdn == NULL) {
					dlog(0, "no userdn has been recorded");
					return 0;
				}
				q = tmp + strlcat(tmp, ctx->userdn,
				    sizeof(tmp));
				break;
			case '%': /* literal % */
				q[0] = p[0];
				q++;
				break;
			default:
				dlog(0, "%s: invalid filter specifier",
				    __func__);
				return NULL;
			}
		} else {
			q[0] = p[0];
			q++;
		}
	}
	if ((size_t) (q - tmp) >= sizeof(tmp)) {
		dlog(0, "filter string too large, unable to process: %s", str);
		return NULL;
	}

	q[0] = '\0';
	q = strdup(tmp);
	if (q == NULL) {
		dlog(0, "%s", strerror(errno));
		return NULL;
	}

	return q;
}
