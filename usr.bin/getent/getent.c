/*	$NetBSD: getent.c,v 1.7 2005/08/24 14:31:02 ginsbach Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Luke Mewburn.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/socket.h>
#include <sys/param.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <net/if.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>		/* for INET6_ADDRSTRLEN */
#include <rpc/rpcent.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <grp.h>
#include <limits.h>
#include <netdb.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <utmpx.h>

static int	usage(void);
static int	parsenum(const char *, unsigned long *);
static int	ethers(int, char *[]);
static int	group(int, char *[]);
static int	hosts(int, char *[]);
static int	netgroup(int, char *[]);
static int	networks(int, char *[]);
static int	passwd(int, char *[]);
static int	protocols(int, char *[]);
static int	rpc(int, char *[]);
static int	services(int, char *[]);
static int	shells(int, char *[]);
static int	utmpx(int, char *[]);

enum {
	RV_OK		= 0,
	RV_USAGE	= 1,
	RV_NOTFOUND	= 2,
	RV_NOENUM	= 3
};

static struct getentdb {
	const char	*name;
	int		(*callback)(int, char *[]);
} databases[] = {
	{	"ethers",	ethers,		},
	{	"group",	group,		},
	{	"hosts",	hosts,		},
	{	"netgroup",	netgroup,	},
	{	"networks",	networks,	},
	{	"passwd",	passwd,		},
	{	"protocols",	protocols,	},
	{	"rpc",		rpc,		},
	{	"services",	services,	},
	{	"shells",	shells,		},
	{	"utmpx",	utmpx,		},

	{	NULL,		NULL,		},
};

int
main(int argc, char *argv[])
{
	struct getentdb	*curdb;

	setprogname(argv[0]);

	if (argc < 2)
		usage();
	for (curdb = databases; curdb->name != NULL; curdb++) {
		if (strcmp(curdb->name, argv[1]) == 0) {
			exit(curdb->callback(argc, argv));
		}
	}
	fprintf(stderr, "Unknown database: %s\n", argv[1]);
	usage();
	/* NOTREACHED */
	return RV_USAGE;
}

static int
usage(void)
{
	struct getentdb	*curdb;

	fprintf(stderr, "Usage: %s database [key ...]\n",
	    getprogname());
	fprintf(stderr, "       database may be one of:\n\t");
	for (curdb = databases; curdb->name != NULL; curdb++) {
		fprintf(stderr, " %s", curdb->name);
	}
	fprintf(stderr, "\n");
	exit(RV_USAGE);
	/* NOTREACHED */
}

static int
parsenum(const char *word, unsigned long *result)
{
	unsigned long	num;
	char		*ep;

	assert(word != NULL);
	assert(result != NULL);

	if (!isdigit((unsigned char)word[0]))
		return 0;
	errno = 0;
	num = strtoul(word, &ep, 10);
	if (num == ULONG_MAX && errno == ERANGE)
		return 0;
	if (*ep != '\0')
		return 0;
	*result = num;
	return 1;
}

/*
 * printfmtstrings --
 *	vprintf(format, ...),
 *	then the aliases (beginning with prefix, separated by sep),
 *	then a newline
 */
static void
printfmtstrings(char *strings[], const char *prefix, const char *sep,
	const char *fmt, ...)
{
	va_list		ap;
	const char	*curpref;
	int		i;

	va_start(ap, fmt);
	vprintf(fmt, ap);

	curpref = prefix;
	for (i = 0; strings[i] != NULL; i++) {
		printf("%s%s", curpref, strings[i]);
		curpref = sep;
	}
	printf("\n");
	va_end(ap);
}

/*
 * ethers
 */
static int
ethers(int argc, char *argv[])
{
	char		hostname[MAXHOSTNAMELEN + 1], *hp;
	struct ether_addr ea, *eap;
	int		i, rv;

	assert(argc > 1);
	assert(argv != NULL);

#define ETHERSPRINT	printf("%-17s  %s\n", ether_ntoa(eap), hp)

	rv = RV_OK;
	if (argc == 2) {
		fprintf(stderr, "Enumeration not supported on ethers\n");
		rv = RV_NOENUM;
	} else {
		for (i = 2; i < argc; i++) {
			if ((eap = ether_aton(argv[i])) == NULL) {
				eap = &ea;
				hp = argv[i];
				if (ether_hostton(hp, eap) != 0) {
					rv = RV_NOTFOUND;
					break;
				}
			} else {
				hp = hostname;
				if (ether_ntohost(hp, eap) != 0) {
					rv = RV_NOTFOUND;
					break;
				}
			}
			ETHERSPRINT;
		}
	}
	return rv;
}

/*
 * group
 */

static int
group(int argc, char *argv[])
{
	struct group	*gr;
	unsigned long	id;
	int		i, rv;

	assert(argc > 1);
	assert(argv != NULL);

#define GROUPPRINT	printfmtstrings(gr->gr_mem, ":", ",", "%s:%s:%u", \
			    gr->gr_name, gr->gr_passwd, gr->gr_gid)

	setgroupent(1);
	rv = RV_OK;
	if (argc == 2) {
		while ((gr = getgrent()) != NULL)
			GROUPPRINT;
	} else {
		for (i = 2; i < argc; i++) {
			if (parsenum(argv[i], &id))
				gr = getgrgid((gid_t)id);
			else
				gr = getgrnam(argv[i]);
			if (gr != NULL)
				GROUPPRINT;
			else {
				rv = RV_NOTFOUND;
				break;
			}
		}
	}
	endgrent();
	return rv;
}


/*
 * hosts
 */

static void
hostsprint(const struct hostent *he)
{
	char	buf[INET6_ADDRSTRLEN];

	assert(he != NULL);
	if (inet_ntop(he->h_addrtype, he->h_addr, buf, sizeof(buf)) == NULL)
		strlcpy(buf, "# unknown", sizeof(buf));
	printfmtstrings(he->h_aliases, "  ", " ", "%-16s  %s", buf, he->h_name);
}

static int
hosts(int argc, char *argv[])
{
	struct hostent	*he4, *he6;
	char		addr[IN6ADDRSZ];
	int		i, rv;

	assert(argc > 1);
	assert(argv != NULL);

	sethostent(1);
	he4 = he6 = NULL;
	rv = RV_OK;
	if (argc == 2) {
		while ((he4 = gethostent()) != NULL)
			hostsprint(he4);
	} else {
		for (i = 2; i < argc; i++) {
			if (inet_pton(AF_INET6, argv[i], (void *)addr) > 0) {
				he6 = gethostbyaddr(addr, IN6ADDRSZ, AF_INET6);
				if (he6 != NULL)
					hostsprint(he6);
			} else if (inet_pton(AF_INET, argv[i],
			    (void *)addr) > 0) {
				he4 = gethostbyaddr(addr, INADDRSZ, AF_INET);
				if (he4 != NULL)
					hostsprint(he4);
	       		} else {
				he6 = gethostbyname2(argv[i], AF_INET6);
				if (he6 != NULL)
					hostsprint(he6);
				he4 = gethostbyname(argv[i]);
				if (he4 != NULL)
					hostsprint(he4);
			}
			if ( he4 == NULL && he6 == NULL ) {
				rv = RV_NOTFOUND;
				break;
			}
		}
	}
	endhostent();
	return rv;
}

/*
 * networks
 */
static void
networksprint(const struct netent *ne)
{
	char		buf[INET6_ADDRSTRLEN];
	struct	in_addr	ianet;

	assert(ne != NULL);
	ianet = inet_makeaddr(ne->n_net, 0);
	if (inet_ntop(ne->n_addrtype, &ianet, buf, sizeof(buf)) == NULL)
		strlcpy(buf, "# unknown", sizeof(buf));
	printfmtstrings(ne->n_aliases, "  ", " ", "%-16s  %s", ne->n_name, buf);
}

static int
networks(int argc, char *argv[])
{
	struct netent	*ne;
	in_addr_t	net;
	int		i, rv;

	assert(argc > 1);
	assert(argv != NULL);

	setnetent(1);
	rv = RV_OK;
	if (argc == 2) {
		while ((ne = getnetent()) != NULL)
			networksprint(ne);
	} else {
		for (i = 2; i < argc; i++) {
			net = inet_network(argv[i]);
			if (net != INADDR_NONE)
				ne = getnetbyaddr(net, AF_INET);
			else
				ne = getnetbyname(argv[i]);
			if (ne != NULL)
				networksprint(ne);
			else {
				rv = RV_NOTFOUND;
				break;
			}
		}
	}
	endnetent();
	return rv;
}

/*
 * passwd
 */
static int
passwd(int argc, char *argv[])
{
	struct passwd	*pw;
	unsigned long	id;
	int		i, rv;

	assert(argc > 1);
	assert(argv != NULL);

#define PASSWDPRINT	printf("%s:%s:%u:%u:%s:%s:%s\n", \
			    pw->pw_name, pw->pw_passwd, pw->pw_uid, \
			    pw->pw_gid, pw->pw_gecos, pw->pw_dir, pw->pw_shell)

	setpassent(1);
	rv = RV_OK;
	if (argc == 2) {
		while ((pw = getpwent()) != NULL)
			PASSWDPRINT;
	} else {
		for (i = 2; i < argc; i++) {
			if (parsenum(argv[i], &id))
				pw = getpwuid((uid_t)id);
			else
				pw = getpwnam(argv[i]);
			if (pw != NULL)
				PASSWDPRINT;
			else {
				rv = RV_NOTFOUND;
				break;
			}
		}
	}
	endpwent();
	return rv;
}

/*
 * protocols
 */
static int
protocols(int argc, char *argv[])
{
	struct protoent	*pe;
	unsigned long	id;
	int		i, rv;

	assert(argc > 1);
	assert(argv != NULL);

#define PROTOCOLSPRINT	printfmtstrings(pe->p_aliases, "  ", " ", \
			    "%-16s  %5d", pe->p_name, pe->p_proto)

	setprotoent(1);
	rv = RV_OK;
	if (argc == 2) {
		while ((pe = getprotoent()) != NULL)
			PROTOCOLSPRINT;
	} else {
		for (i = 2; i < argc; i++) {
			if (parsenum(argv[i], &id))
				pe = getprotobynumber((int)id);
			else
				pe = getprotobyname(argv[i]);
			if (pe != NULL)
				PROTOCOLSPRINT;
			else {
				rv = RV_NOTFOUND;
				break;
			}
		}
	}
	endprotoent();
	return rv;
}

/*
 * rpc
 */
static int
rpc(int argc, char *argv[])
{
	struct rpcent	*re;
	unsigned long	id;
	int		i, rv;

	assert(argc > 1);
	assert(argv != NULL);

#define RPCPRINT	printfmtstrings(re->r_aliases, "  ", " ", \
				"%-16s  %6d", \
				re->r_name, re->r_number)

	setrpcent(1);
	rv = RV_OK;
	if (argc == 2) {
		while ((re = getrpcent()) != NULL)
			RPCPRINT;
	} else {
		for (i = 2; i < argc; i++) {
			if (parsenum(argv[i], &id))
				re = getrpcbynumber((int)id);
			else
				re = getrpcbyname(argv[i]);
			if (re != NULL)
				RPCPRINT;
			else {
				rv = RV_NOTFOUND;
				break;
			}
		}
	}
	endrpcent();
	return rv;
}

/*
 * services
 */
static int
services(int argc, char *argv[])
{
	struct servent	*se;
	unsigned long	id;
	char		*proto;
	int		i, rv;

	assert(argc > 1);
	assert(argv != NULL);

#define SERVICESPRINT	printfmtstrings(se->s_aliases, "  ", " ", \
			    "%-16s  %5d/%s", \
			    se->s_name, ntohs(se->s_port), se->s_proto)

	setservent(1);
	rv = RV_OK;
	if (argc == 2) {
		while ((se = getservent()) != NULL)
			SERVICESPRINT;
	} else {
		for (i = 2; i < argc; i++) {
			proto = strchr(argv[i], '/');
			if (proto != NULL)
				*proto++ = '\0';
			if (parsenum(argv[i], &id))
				se = getservbyport(htons((u_short)id), proto);
			else
				se = getservbyname(argv[i], proto);
			if (se != NULL)
				SERVICESPRINT;
			else {
				rv = RV_NOTFOUND;
				break;
			}
		}
	}
	endservent();
	return rv;
}

/*
 * shells
 */
static int
shells(int argc, char *argv[])
{
	const char	*sh;
	int		i, rv;

	assert(argc > 1);
	assert(argv != NULL);

#define SHELLSPRINT	printf("%s\n", sh)

	setusershell();
	rv = RV_OK;
	if (argc == 2) {
		while ((sh = getusershell()) != NULL)
			SHELLSPRINT;
	} else {
		for (i = 2; i < argc; i++) {
			setusershell();
			while ((sh = getusershell()) != NULL) {
				if (strcmp(sh, argv[i]) == 0) {
					SHELLSPRINT;
					break;
				}
			}
			if (sh == NULL) {
				rv = RV_NOTFOUND;
				break;
			}
		}
	}
	endusershell();
	return rv;
}

/*
 * netgroup
 */
static int
netgroup(int argc, char *argv[])
{
	char		*host, *user, *domain;
	int		first;
	int		rv, i;

	assert(argc > 1);
	assert(argv != NULL);

#define NETGROUPPRINT(s)	(((s) != NULL) ? (s) : "")

	rv = RV_OK;
	if (argc == 2) {
		fprintf(stderr, "Enumeration not supported on netgroup\n");
		rv = RV_NOENUM;
	} else {
		for (i = 2; i < argc; i++) {
			setnetgrent(argv[i]);
			first = 1;
			while (getnetgrent(&host, &user, &domain) != 0) {
				if (first) {
					first = 0;
					(void)fputs(argv[i], stdout);
				}
				(void)printf(" (%s,%s,%s)",
				    NETGROUPPRINT(host),
				    NETGROUPPRINT(user),
				    NETGROUPPRINT(domain));
			}
			if (!first)
				(void)putchar('\n');
			endnetgrent();
		}
	}
	return rv;
}

/*
 * utmpx
 */

#define	UTMPXPRINTID do {			\
	size_t i;				\
	for (i = 0; i < sizeof ut->ut_id; i++)	\
		printf("%02hhx", ut->ut_id[i]);	\
} while (0)

static void
utmpxprint(const struct utmpx *ut)
{

	if (ut->ut_type == EMPTY)
		return;
	
	printf("[%jd.%06u -- %.24s] ",
	    (intmax_t)ut->ut_tv.tv_sec, (unsigned int)ut->ut_tv.tv_usec,
	    ctime(&ut->ut_tv.tv_sec));

	switch (ut->ut_type) {
	case BOOT_TIME:
		printf("system boot\n");
		return;
	case SHUTDOWN_TIME:
		printf("system shutdown\n");
		return;
	case OLD_TIME:
		printf("old system time\n");
		return;
	case NEW_TIME:
		printf("new system time\n");
		return;
	case USER_PROCESS:
		printf("user process: id=\"");
		UTMPXPRINTID;
		printf("\" pid=\"%d\" user=\"%s\" line=\"%s\" host=\"%s\"\n",
		    ut->ut_pid, ut->ut_user, ut->ut_line, ut->ut_host);
		break;
	case INIT_PROCESS:
		printf("init process: id=\"");
		UTMPXPRINTID;
		printf("\" pid=\"%d\"\n", ut->ut_pid);
		break;
	case LOGIN_PROCESS:
		printf("login process: id=\"");
		UTMPXPRINTID;
		printf("\" pid=\"%d\" user=\"%s\" line=\"%s\" host=\"%s\"\n",
		    ut->ut_pid, ut->ut_user, ut->ut_line, ut->ut_host);
		break;
	case DEAD_PROCESS:
		printf("dead process: id=\"");
		UTMPXPRINTID;
		printf("\" pid=\"%d\"\n", ut->ut_pid);
		break;
	default:
		printf("unknown record type %hu\n", ut->ut_type);
		break;
	}
}

static int
utmpx(int argc, char *argv[])
{
	const struct utmpx *ut;
	const char *file = NULL;
	int rv = RV_OK, db = 0;

	assert(argc > 1);
	assert(argv != NULL);

	if (argc == 3 || argc == 4) {
		if (strcmp(argv[2], "active") == 0)
			db = UTXDB_ACTIVE;
		else if (strcmp(argv[2], "lastlogin") == 0)
			db = UTXDB_LASTLOGIN;
		else if (strcmp(argv[2], "log") == 0)
			db = UTXDB_LOG;
		else
			rv = RV_USAGE;
		if (argc == 4)
			file = argv[3];
	} else {
		rv = RV_USAGE;
	}

	if (rv == RV_USAGE) {
		fprintf(stderr,
		    "Usage: %s utmpx active | lastlogin | log [filename]\n",
		    getprogname());
	} else if (rv == RV_OK) {
		if (setutxdb(db, file) != 0)
			return (RV_NOTFOUND);
		while ((ut = getutxent()) != NULL)
			utmpxprint(ut);
		endutxent();
	}
	return (rv);
}
