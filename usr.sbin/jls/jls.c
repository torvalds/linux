/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Mike Barcroft <mike@FreeBSD.org>
 * Copyright (c) 2008 Bjoern A. Zeeb <bz@FreeBSD.org>
 * Copyright (c) 2009 James Gritton <jamie@FreeBSD.org>
 * Copyright (c) 2015 Emmanuel Vadot <manu@bocal.org>
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include <sys/param.h>
#include <sys/jail.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <err.h>
#include <errno.h>
#include <jail.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libxo/xo.h>

#define	JP_USER		0x01000000
#define	JP_OPT		0x02000000

#define JLS_XO_VERSION	"2"

#define	PRINT_DEFAULT	0x01
#define	PRINT_HEADER	0x02
#define	PRINT_NAMEVAL	0x04
#define	PRINT_QUOTED	0x08
#define	PRINT_SKIP	0x10
#define	PRINT_VERBOSE	0x20
#define	PRINT_JAIL_NAME	0x40

static struct jailparam *params;
static int *param_parent;
static int nparams;
#ifdef INET6
static int ip6_ok;
#endif
#ifdef INET
static int ip4_ok;
#endif

static int add_param(const char *name, void *value, size_t valuelen,
		struct jailparam *source, unsigned flags);
static int sort_param(const void *a, const void *b);
static char *noname(const char *name);
static char *nononame(const char *name);
static int print_jail(int pflags, int jflags);
static int special_print(int pflags, struct jailparam *param);
static void quoted_print(int pflags, char *name, char *value);
static void emit_ip_addr_list(int af_family, const char *list_name,
		struct jailparam *param);

int
main(int argc, char **argv)
{
	char *dot, *ep, *jname, *pname;
	int c, i, jflags, jid, lastjid, pflags, spc;

	argc = xo_parse_args(argc, argv);
	if (argc < 0)
		exit(1);

        xo_set_version(JLS_XO_VERSION);
	jname = NULL;
	pflags = jflags = jid = 0;
	while ((c = getopt(argc, argv, "adj:hNnqsv")) >= 0)
		switch (c) {
		case 'a':
		case 'd':
			jflags |= JAIL_DYING;
			break;
		case 'j':
			jid = strtoul(optarg, &ep, 10);
			if (!jid || *ep) {
				jid = 0;
				jname = optarg;
			}
			break;
		case 'h':
			pflags = (pflags & ~(PRINT_SKIP | PRINT_VERBOSE)) |
			    PRINT_HEADER;
			break;
		case 'N':
			pflags |= PRINT_JAIL_NAME;
			break;
		case 'n':
			pflags = (pflags & ~PRINT_VERBOSE) | PRINT_NAMEVAL;
			break;
		case 'q':
			pflags |= PRINT_QUOTED;
			break;
		case 's':
			pflags = (pflags & ~(PRINT_HEADER | PRINT_VERBOSE)) |
			    PRINT_NAMEVAL | PRINT_QUOTED | PRINT_SKIP;
			break;
		case 'v':
			pflags = (pflags &
			    ~(PRINT_HEADER | PRINT_NAMEVAL | PRINT_SKIP)) |
			    PRINT_VERBOSE;
			break;
		default:
			xo_errx(1, "usage: jls [-dhNnqv] [-j jail] [param ...]");
		}

#ifdef INET6
	ip6_ok = feature_present("inet6");
#endif
#ifdef INET
	ip4_ok = feature_present("inet");
#endif

	/* Add the parameters to print. */
	if (optind == argc) {
		if (pflags & (PRINT_HEADER | PRINT_NAMEVAL))
			add_param("all", NULL, (size_t)0, NULL, JP_USER);
		else if (pflags & PRINT_VERBOSE) {
			add_param("jid", NULL, (size_t)0, NULL, JP_USER);
			add_param("host.hostname", NULL, (size_t)0, NULL,
			    JP_USER);
			add_param("path", NULL, (size_t)0, NULL, JP_USER);
			add_param("name", NULL, (size_t)0, NULL, JP_USER);
			add_param("dying", NULL, (size_t)0, NULL, JP_USER);
			add_param("cpuset.id", NULL, (size_t)0, NULL, JP_USER);
#ifdef INET
			if (ip4_ok)
				add_param("ip4.addr", NULL, (size_t)0, NULL,
				    JP_USER);
#endif
#ifdef INET6
			if (ip6_ok)
				add_param("ip6.addr", NULL, (size_t)0, NULL,
				    JP_USER | JP_OPT);
#endif
		} else {
			pflags |= PRINT_DEFAULT;
			if (pflags & PRINT_JAIL_NAME)
				add_param("name", NULL, (size_t)0, NULL, JP_USER);
			else
				add_param("jid", NULL, (size_t)0, NULL, JP_USER);
#ifdef INET
			if (ip4_ok)
				add_param("ip4.addr", NULL, (size_t)0, NULL,
				    JP_USER);
#endif
			add_param("host.hostname", NULL, (size_t)0, NULL,
			    JP_USER);
			add_param("path", NULL, (size_t)0, NULL, JP_USER);
		}
	} else {
		pflags &= ~PRINT_VERBOSE;
		while (optind < argc)
			add_param(argv[optind++], NULL, (size_t)0, NULL,
			    JP_USER);
	}

	if (pflags & PRINT_SKIP) {
		/* Check for parameters with jailsys parents. */
		for (i = 0; i < nparams; i++) {
			if ((params[i].jp_flags & JP_USER) &&
			    (dot = strchr(params[i].jp_name, '.'))) {
				pname = alloca((dot - params[i].jp_name) + 1);
				strlcpy(pname, params[i].jp_name,
				    (dot - params[i].jp_name) + 1);
				param_parent[i] = add_param(pname,
				    NULL, (size_t)0, NULL, JP_OPT);
			}
		}
	}

	/* Add the index key parameters. */
	if (jid != 0)
		add_param("jid", &jid, sizeof(jid), NULL, 0);
	else if (jname != NULL)
		add_param("name", jname, strlen(jname), NULL, 0);
	else
		add_param("lastjid", &lastjid, sizeof(lastjid), NULL, 0);

	/* Print a header line if requested. */
	if (pflags & PRINT_VERBOSE) {
		xo_emit("{T:/%3s}{T:JID}{P:  }{T:Hostname}{Pd:/%22s}{T:Path}\n",
		        "", "");
		xo_emit("{P:/%8s}{T:Name}{Pd:/%26s}{T:State}\n", "", "");
		xo_emit("{P:/%8s}{T:CPUSetID}\n", "");
		xo_emit("{P:/%8s}{T:IP Address(es)}\n", "");
	}
	else if (pflags & PRINT_DEFAULT)
		if (pflags & PRINT_JAIL_NAME)
			xo_emit("{P: }{T:JID/%-15s}{P: }{T:IP Address/%-15s}"
			        "{P: }{T:Hostname/%-29s}{P: }{T:Path}\n");
		else
			xo_emit("{T:JID/%6s}{P:  }{T:IP Address}{P:/%6s}"
			        "{T:Hostname}{P:/%22s}{T:Path}\n", "", "");
	else if (pflags & PRINT_HEADER) {
		for (i = spc = 0; i < nparams; i++)
			if (params[i].jp_flags & JP_USER) {
				if (spc)
					xo_emit("{P: }");
				else
					spc = 1;
				xo_emit(params[i].jp_name);
			}
		xo_emit("{P:\n}");
	}

	xo_open_container("jail-information");
	xo_open_list("jail");
	/* Fetch the jail(s) and print the parameters. */
	if (jid != 0 || jname != NULL) {
		if (print_jail(pflags, jflags) < 0)
			xo_errx(1, "%s", jail_errmsg);
	} else {
		for (lastjid = 0;
		     (lastjid = print_jail(pflags, jflags)) >= 0; )
			;
		if (errno != 0 && errno != ENOENT)
			xo_errx(1, "%s", jail_errmsg);
	}
	xo_close_list("jail");
	xo_close_container("jail-information");
	xo_finish();
	return (0);
}

static int
add_param(const char *name, void *value, size_t valuelen,
    struct jailparam *source, unsigned flags)
{
	struct jailparam *param, *tparams;
	int i, tnparams;

	static int paramlistsize;

	/* The pseudo-parameter "all" scans the list of available parameters. */
	if (!strcmp(name, "all")) {
		tnparams = jailparam_all(&tparams);
		if (tnparams < 0)
			xo_errx(1, "%s", jail_errmsg);
		qsort(tparams, (size_t)tnparams, sizeof(struct jailparam),
		    sort_param);
		for (i = 0; i < tnparams; i++)
			add_param(tparams[i].jp_name, NULL, (size_t)0,
			    tparams + i, flags);
		free(tparams);
		return -1;
	}

	/* Check for repeat parameters. */
	for (i = 0; i < nparams; i++)
		if (!strcmp(name, params[i].jp_name)) {
			if (value != NULL && jailparam_import_raw(params + i,
			    value, valuelen) < 0)
				xo_errx(1, "%s", jail_errmsg);
			params[i].jp_flags |= flags;
			if (source != NULL)
				jailparam_free(source, 1);
			return i;
		}

	/* Make sure there is room for the new param record. */
	if (!nparams) {
		paramlistsize = 32;
		params = malloc(paramlistsize * sizeof(*params));
		param_parent = malloc(paramlistsize * sizeof(*param_parent));
		if (params == NULL || param_parent == NULL)
			xo_err(1, "malloc");
	} else if (nparams >= paramlistsize) {
		paramlistsize *= 2;
		params = realloc(params, paramlistsize * sizeof(*params));
		param_parent = realloc(param_parent,
		    paramlistsize * sizeof(*param_parent));
		if (params == NULL || param_parent == NULL)
			xo_err(1, "realloc");
	}

	/* Look up the parameter. */
	param_parent[nparams] = -1;
	param = params + nparams++;
	if (source != NULL) {
		*param = *source;
		param->jp_flags |= flags;
		return param - params;
	}
	if (jailparam_init(param, name) < 0 ||
	    (value != NULL ? jailparam_import_raw(param, value, valuelen)
	     : jailparam_import(param, value)) < 0) {
		if (flags & JP_OPT) {
			nparams--;
			return (-1);
		}
		xo_errx(1, "%s", jail_errmsg);
	}
	param->jp_flags = flags;
	return param - params;
}

static int
sort_param(const void *a, const void *b)
{
	const struct jailparam *parama, *paramb;
	char *ap, *bp;

	/* Put top-level parameters first. */
	parama = a;
	paramb = b;
	ap = strchr(parama->jp_name, '.');
	bp = strchr(paramb->jp_name, '.');
	if (ap && !bp)
		return (1);
	if (bp && !ap)
		return (-1);
	return (strcmp(parama->jp_name, paramb->jp_name));
}

static char *
noname(const char *name)
{
	char *nname, *p;

	nname = malloc(strlen(name) + 3);
	if (nname == NULL)
		xo_err(1, "malloc");
	p = strrchr(name, '.');
	if (p != NULL)
		sprintf(nname, "%.*s.no%s", (int)(p - name), name, p + 1);
	else
		sprintf(nname, "no%s", name);
	return nname;
}

static char *
nononame(const char *name)
{
	char *nname, *p;

	p = strrchr(name, '.');
	if (strncmp(p ? p + 1 : name, "no", 2))
		return NULL;
	nname = malloc(strlen(name) - 1);
	if (nname == NULL)
		xo_err(1, "malloc");
	if (p != NULL)
		sprintf(nname, "%.*s.%s", (int)(p - name), name, p + 3);
	else
		strcpy(nname, name + 2);
	return nname;
}

static int
print_jail(int pflags, int jflags)
{
	char *nname, *xo_nname;
	char **param_values;
	int i, jid, n, spc;

	jid = jailparam_get(params, nparams, jflags);
	if (jid < 0)
		return jid;

	xo_open_instance("jail");

	if (pflags & PRINT_VERBOSE) {
		xo_emit("{:jid/%6d}{P:  }{:hostname/%-29.29s/%s}{P: }"
		    "{:path/%.74s/%s}\n",
		    *(int *)params[0].jp_value,
		    (char *)params[1].jp_value,
		    (char *)params[2].jp_value);
		xo_emit("{P:        }{:name/%-29.29s/%s}{P: }{:state/%.74s}\n",
		    (char *)params[3].jp_value,
		    *(int *)params[4].jp_value ? "DYING" : "ACTIVE");
		xo_emit("{P:        }{:cpusetid/%d}\n", *(int *)params[5].jp_value);
		n = 6;
#ifdef INET
		if (ip4_ok && !strcmp(params[n].jp_name, "ip4.addr")) {
			emit_ip_addr_list(AF_INET, "ipv4_addrs", params + n);
			n++;
		}
#endif
#ifdef INET6
		if (ip6_ok && !strcmp(params[n].jp_name, "ip6.addr")) {
			emit_ip_addr_list(AF_INET6, "ipv6_addrs", params + n);
			n++;
		}
#endif
	} else if (pflags & PRINT_DEFAULT) {
		if (pflags & PRINT_JAIL_NAME)
			xo_emit("{P: }{:name/%-15s/%s}{P: }",
			    (char *)params[0].jp_value);
		else
			xo_emit("{:jid/%6d}{P:  }", *(int *)params[0].jp_value);
		xo_emit("{:ipv4/%-15.15s/%s}{P: }{:hostname/%-29.29s/%s}{P: }{:path/%.74s/%s}\n",
#ifdef INET
		    (!ip4_ok || params[1].jp_valuelen == 0) ? ""
		    : inet_ntoa(*(struct in_addr *)params[1].jp_value),
		    (char *)params[2-!ip4_ok].jp_value,
		    (char *)params[3-!ip4_ok].jp_value);
#else
		    "-",
		    (char *)params[1].jp_value,
		    (char *)params[2].jp_value);
#endif
	} else {
		param_values = alloca(nparams * sizeof(*param_values));
		for (i = 0; i < nparams; i++) {
			if (!(params[i].jp_flags & JP_USER))
				continue;
			param_values[i] = jailparam_export(params + i);
			if (param_values[i] == NULL)
				xo_errx(1, "%s", jail_errmsg);
		}
		for (i = spc = 0; i < nparams; i++) {
			if (!(params[i].jp_flags & JP_USER))
				continue;
			if ((pflags & PRINT_SKIP) &&
			    ((!(params[i].jp_ctltype &
				(CTLFLAG_WR | CTLFLAG_TUN))) ||
			     (param_parent[i] >= 0 &&
			      *(int *)params[param_parent[i]].jp_value !=
			      JAIL_SYS_NEW)))
				continue;
			if (spc)
				xo_emit("{P: }");
			else
				spc = 1;
			if (pflags & PRINT_NAMEVAL) {
				/*
				 * Generally "name=value", but for booleans
				 * either "name" or "noname".
				 */
				if (params[i].jp_flags &
				    (JP_BOOL | JP_NOBOOL)) {
					if (*(int *)params[i].jp_value) {
						asprintf(&xo_nname, "{en:%s/true}", params[i].jp_name);
						xo_emit(xo_nname);
						xo_emit("{d:/%s}", params[i].jp_name);
					}
					else {
						nname = (params[i].jp_flags &
						    JP_NOBOOL) ?
						    nononame(params[i].jp_name)
						    : noname(params[i].jp_name);
						if (params[i].jp_flags & JP_NOBOOL) {
							asprintf(&xo_nname, "{en:%s/true}", params[i].jp_name);
							xo_emit(xo_nname);
						} else {
							asprintf(&xo_nname, "{en:%s/false}", params[i].jp_name);
							xo_emit(xo_nname);
						}
						xo_emit("{d:/%s}", nname);
						free(nname);
					}
					free(xo_nname);
					continue;
				}
				xo_emit("{d:%s}=", params[i].jp_name);
			}
			if (!special_print(pflags, params + i))
				quoted_print(pflags, params[i].jp_name, param_values[i]);
		}
		xo_emit("{P:\n}");
		for (i = 0; i < nparams; i++)
			if (params[i].jp_flags & JP_USER)
				free(param_values[i]);
	}

	xo_close_instance("jail");
	return (jid);
}

static void
quoted_print(int pflags, char *name, char *value)
{
	int qc;
	char *p = value;
	char *param_name_value;

	/* An empty string needs quoting. */
	if (!*p) {
		asprintf(&param_name_value, "{k:%s}{d:%s/\"\"}", name, name);
		xo_emit(param_name_value);
		free(param_name_value);
		return;
	}

	asprintf(&param_name_value, "{:%s/%%s}", name);
	/*
	 * The value will be surrounded by quotes if it contains spaces
	 * or quotes.
	 */
	qc = strchr(p, '\'') ? '"'
		: strchr(p, '"') ? '\''
		: strchr(p, ' ') || strchr(p, '\t') ? '"'
		: 0;

	if (qc && pflags & PRINT_QUOTED)
		xo_emit("{P:/%c}", qc);

	xo_emit(param_name_value, value);

	free(param_name_value);

	if (qc && pflags & PRINT_QUOTED)
		xo_emit("{P:/%c}", qc);
}

static int
special_print(int pflags, struct jailparam *param)
{
	int ip_as_list;

	switch (xo_get_style(NULL)) {
	case XO_STYLE_JSON:
	case XO_STYLE_XML:
		ip_as_list = 1;
		break;
	default:
		ip_as_list = 0;
	}

	if (!ip_as_list && param->jp_valuelen == 0) {
		if (pflags & PRINT_QUOTED)
			xo_emit("{P:\"\"}");
		else if (!(pflags & PRINT_NAMEVAL))
			xo_emit("{P:-}");
	} else if (ip_as_list && !strcmp(param->jp_name, "ip4.addr")) {
		emit_ip_addr_list(AF_INET, param->jp_name, param);
	} else if (ip_as_list && !strcmp(param->jp_name, "ip6.addr")) {
		emit_ip_addr_list(AF_INET6, param->jp_name, param);
	} else {
		return 0;
	}

	return 1;
}

static void
emit_ip_addr_list(int af_family, const char *list_name, struct jailparam *param)
{
	char ipbuf[INET6_ADDRSTRLEN];
	size_t addr_len;
	const char *emit_str;
	int ai, count;

	switch (af_family) {
	case AF_INET:
		addr_len = sizeof(struct in_addr);
		emit_str = "{P:        }{ql:ipv4_addr}{P:\n}";
		break;
	case AF_INET6:
		addr_len = sizeof(struct in6_addr);
		emit_str = "{P:        }{ql:ipv6_addr}{P:\n}";
		break;
	default:
		xo_err(1, "unsupported af_family");
		return;
	}

	count = param->jp_valuelen / addr_len;

	xo_open_list(list_name);
	for (ai = 0; ai < count; ai++) {
		if (inet_ntop(af_family,
		    ((uint8_t *)param->jp_value) + addr_len * ai,
		    ipbuf, sizeof(ipbuf)) == NULL) {
			xo_err(1, "inet_ntop");
		} else {
			xo_emit(emit_str, ipbuf);
		}
	}
	xo_close_list(list_name);
}
