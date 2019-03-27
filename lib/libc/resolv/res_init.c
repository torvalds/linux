/*-
 * SPDX-License-Identifier: (BSD-3-Clause AND ISC)
 *
 * Copyright (c) 1985, 1989, 1993
 *    The Regents of the University of California.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
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
 */

/*
 * Portions Copyright (c) 1993 by Digital Equipment Corporation.
 * 
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies, and that
 * the name of Digital Equipment Corporation not be used in advertising or
 * publicity pertaining to distribution of the document or software without
 * specific, written prior permission.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND DIGITAL EQUIPMENT CORP. DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS.   IN NO EVENT SHALL DIGITAL EQUIPMENT
 * CORPORATION BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */

/*
 * Copyright (c) 2004 by Internet Systems Consortium, Inc. ("ISC")
 * Portions Copyright (c) 1996-1999 by Internet Software Consortium.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#if defined(LIBC_SCCS) && !defined(lint)
static const char sccsid[] = "@(#)res_init.c	8.1 (Berkeley) 6/7/93";
static const char rcsid[] = "$Id: res_init.c,v 1.26 2008/12/11 09:59:00 marka Exp $";
#endif /* LIBC_SCCS and not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "port_before.h"

#include "namespace.h"

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>

#ifndef HAVE_MD5
# include "../dst/md5.h"
#else
# ifdef SOLARIS2
#  include <sys/md5.h>
# elif _LIBC
# include <md5.h>
# endif
#endif
#ifndef _MD5_H_
# define _MD5_H_ 1	/*%< make sure we do not include rsaref md5.h file */
#endif

#include "un-namespace.h"

#include "port_after.h"

/* ensure that sockaddr_in6 and IN6ADDR_ANY_INIT are declared / defined */
#include <resolv.h>

#include "res_private.h"

/*% Options.  Should all be left alone. */
#define RESOLVSORT
#ifndef	DEBUG
#define	DEBUG
#endif

#ifdef SOLARIS2
#include <sys/systeminfo.h>
#endif

static void res_setoptions(res_state, const char *, const char *);

#ifdef RESOLVSORT
static const char sort_mask[] = "/&";
#define ISSORTMASK(ch) (strchr(sort_mask, ch) != NULL)
static u_int32_t net_mask(struct in_addr);
#endif

#if !defined(isascii)	/*%< XXX - could be a function */
# define isascii(c) (!(c & 0200))
#endif

/*
 * Resolver state default settings.
 */

/*%
 * Set up default settings.  If the configuration file exist, the values
 * there will have precedence.  Otherwise, the server address is set to
 * INADDR_ANY and the default domain name comes from the gethostname().
 *
 * An interim version of this code (BIND 4.9, pre-4.4BSD) used 127.0.0.1
 * rather than INADDR_ANY ("0.0.0.0") as the default name server address
 * since it was noted that INADDR_ANY actually meant ``the first interface
 * you "ifconfig"'d at boot time'' and if this was a SLIP or PPP interface,
 * it had to be "up" in order for you to reach your own name server.  It
 * was later decided that since the recommended practice is to always 
 * install local static routes through 127.0.0.1 for all your network
 * interfaces, that we could solve this problem without a code change.
 *
 * The configuration file should always be used, since it is the only way
 * to specify a default domain.  If you are running a server on your local
 * machine, you should say "nameserver 0.0.0.0" or "nameserver 127.0.0.1"
 * in the configuration file.
 *
 * Return 0 if completes successfully, -1 on error
 */
int
res_ninit(res_state statp) {
	extern int __res_vinit(res_state, int);

	return (__res_vinit(statp, 0));
}

/*% This function has to be reachable by res_data.c but not publicly. */
int
__res_vinit(res_state statp, int preinit) {
	FILE *fp;
	char *cp, **pp;
	int n;
	char buf[BUFSIZ];
	int nserv = 0;    /*%< number of nameserver records read from file */
	int haveenv = 0;
	int havesearch = 0;
#ifdef RESOLVSORT
	int nsort = 0;
	char *net;
#endif
	int dots;
	union res_sockaddr_union u[2];
	int maxns = MAXNS;

	RES_SET_H_ERRNO(statp, 0);
	if (statp->_u._ext.ext != NULL)
		res_ndestroy(statp);

	if (!preinit) {
		statp->retrans = RES_TIMEOUT;
		statp->retry = RES_DFLRETRY;
		statp->options = RES_DEFAULT;
	}

	statp->_rnd = malloc(16);
	res_rndinit(statp);
	statp->id = res_nrandomid(statp);

	memset(u, 0, sizeof(u));
#ifdef USELOOPBACK
	u[nserv].sin.sin_addr = inet_makeaddr(IN_LOOPBACKNET, 1);
#else
	u[nserv].sin.sin_addr.s_addr = INADDR_ANY;
#endif
	u[nserv].sin.sin_family = AF_INET;
	u[nserv].sin.sin_port = htons(NAMESERVER_PORT);
#ifdef HAVE_SA_LEN
	u[nserv].sin.sin_len = sizeof(struct sockaddr_in);
#endif
	nserv++;
#ifdef HAS_INET6_STRUCTS
#ifdef USELOOPBACK
	u[nserv].sin6.sin6_addr = in6addr_loopback;
#else
	u[nserv].sin6.sin6_addr = in6addr_any;
#endif
	u[nserv].sin6.sin6_family = AF_INET6;
	u[nserv].sin6.sin6_port = htons(NAMESERVER_PORT);
#ifdef HAVE_SA_LEN
	u[nserv].sin6.sin6_len = sizeof(struct sockaddr_in6);
#endif
	nserv++;
#endif
	statp->nscount = 0;
	statp->ndots = 1;
	statp->pfcode = 0;
	statp->_vcsock = -1;
	statp->_flags = 0;
	statp->qhook = NULL;
	statp->rhook = NULL;
	statp->_u._ext.nscount = 0;
	statp->_u._ext.ext = malloc(sizeof(*statp->_u._ext.ext));
	if (statp->_u._ext.ext != NULL) {
	        memset(statp->_u._ext.ext, 0, sizeof(*statp->_u._ext.ext));
		statp->_u._ext.ext->nsaddrs[0].sin = statp->nsaddr;
		strcpy(statp->_u._ext.ext->nsuffix, "ip6.arpa");
		strcpy(statp->_u._ext.ext->nsuffix2, "ip6.int");
		statp->_u._ext.ext->reload_period = 2;
	} else {
		/*
		 * Historically res_init() rarely, if at all, failed.
		 * Examples and applications exist which do not check
		 * our return code.  Furthermore several applications
		 * simply call us to get the systems domainname.  So
		 * rather then immediately fail here we store the
		 * failure, which is returned later, in h_errno.  And
		 * prevent the collection of 'nameserver' information
		 * by setting maxns to 0.  Thus applications that fail
		 * to check our return code wont be able to make
		 * queries anyhow.
		 */
		RES_SET_H_ERRNO(statp, NETDB_INTERNAL);
		maxns = 0;
	}
#ifdef RESOLVSORT
	statp->nsort = 0;
#endif
	res_setservers(statp, u, nserv);

#ifdef	SOLARIS2
	/*
	 * The old libresolv derived the defaultdomain from NIS/NIS+.
	 * We want to keep this behaviour
	 */
	{
		char buf[sizeof(statp->defdname)], *cp;
		int ret;

		if ((ret = sysinfo(SI_SRPC_DOMAIN, buf, sizeof(buf))) > 0 &&
			(unsigned int)ret <= sizeof(buf)) {
			if (buf[0] == '+')
				buf[0] = '.';
			cp = strchr(buf, '.');
			cp = (cp == NULL) ? buf : (cp + 1);
			strncpy(statp->defdname, cp,
				sizeof(statp->defdname) - 1);
			statp->defdname[sizeof(statp->defdname) - 1] = '\0';
		}
	}
#endif	/* SOLARIS2 */

	/* Allow user to override the local domain definition */
	if (issetugid() == 0 && (cp = getenv("LOCALDOMAIN")) != NULL) {
		(void)strncpy(statp->defdname, cp, sizeof(statp->defdname) - 1);
		statp->defdname[sizeof(statp->defdname) - 1] = '\0';
		haveenv++;

		/*
		 * Set search list to be blank-separated strings
		 * from rest of env value.  Permits users of LOCALDOMAIN
		 * to still have a search list, and anyone to set the
		 * one that they want to use as an individual (even more
		 * important now that the rfc1535 stuff restricts searches)
		 */
		cp = statp->defdname;
		pp = statp->dnsrch;
		*pp++ = cp;
		for (n = 0; *cp && pp < statp->dnsrch + MAXDNSRCH; cp++) {
			if (*cp == '\n')	/*%< silly backwards compat */
				break;
			else if (*cp == ' ' || *cp == '\t') {
				*cp = 0;
				n = 1;
			} else if (n) {
				*pp++ = cp;
				n = 0;
				havesearch = 1;
			}
		}
		/* null terminate last domain if there are excess */
		while (*cp != '\0' && *cp != ' ' && *cp != '\t' && *cp != '\n')
			cp++;
		*cp = '\0';
		*pp++ = NULL;
	}

#define	MATCH(line, name) \
	(!strncmp(line, name, sizeof(name) - 1) && \
	(line[sizeof(name) - 1] == ' ' || \
	 line[sizeof(name) - 1] == '\t'))

	nserv = 0;
	if ((fp = fopen(_PATH_RESCONF, "re")) != NULL) {
	    struct stat sb;
	    struct timespec now;

	    if (statp->_u._ext.ext != NULL) {
		if (_fstat(fileno(fp), &sb) == 0) {
		    statp->_u._ext.ext->conf_mtim = sb.st_mtim;
		    if (clock_gettime(CLOCK_MONOTONIC_FAST, &now) == 0) {
			statp->_u._ext.ext->conf_stat = now.tv_sec;
		    }
		}
	    }

	    /* read the config file */
	    while (fgets(buf, sizeof(buf), fp) != NULL) {
		/* skip comments */
		if (*buf == ';' || *buf == '#')
			continue;
		/* read default domain name */
		if (MATCH(buf, "domain")) {
		    if (haveenv)	/*%< skip if have from environ */
			    continue;
		    cp = buf + sizeof("domain") - 1;
		    while (*cp == ' ' || *cp == '\t')
			    cp++;
		    if ((*cp == '\0') || (*cp == '\n'))
			    continue;
		    strncpy(statp->defdname, cp, sizeof(statp->defdname) - 1);
		    statp->defdname[sizeof(statp->defdname) - 1] = '\0';
		    if ((cp = strpbrk(statp->defdname, " \t\n")) != NULL)
			    *cp = '\0';
		    havesearch = 0;
		    continue;
		}
		/* set search list */
		if (MATCH(buf, "search")) {
		    if (haveenv)	/*%< skip if have from environ */
			    continue;
		    cp = buf + sizeof("search") - 1;
		    while (*cp == ' ' || *cp == '\t')
			    cp++;
		    if ((*cp == '\0') || (*cp == '\n'))
			    continue;
		    strncpy(statp->defdname, cp, sizeof(statp->defdname) - 1);
		    statp->defdname[sizeof(statp->defdname) - 1] = '\0';
		    if ((cp = strchr(statp->defdname, '\n')) != NULL)
			    *cp = '\0';
		    /*
		     * Set search list to be blank-separated strings
		     * on rest of line.
		     */
		    cp = statp->defdname;
		    pp = statp->dnsrch;
		    *pp++ = cp;
		    for (n = 0; *cp && pp < statp->dnsrch + MAXDNSRCH; cp++) {
			    if (*cp == ' ' || *cp == '\t') {
				    *cp = 0;
				    n = 1;
			    } else if (n) {
				    *pp++ = cp;
				    n = 0;
			    }
		    }
		    /* null terminate last domain if there are excess */
		    while (*cp != '\0' && *cp != ' ' && *cp != '\t')
			    cp++;
		    *cp = '\0';
		    *pp++ = NULL;
		    havesearch = 1;
		    continue;
		}
		/* read nameservers to query */
		if (MATCH(buf, "nameserver") && nserv < maxns) {
		    struct addrinfo hints, *ai;
		    char sbuf[NI_MAXSERV];
		    const size_t minsiz =
		        sizeof(statp->_u._ext.ext->nsaddrs[0]);

		    cp = buf + sizeof("nameserver") - 1;
		    while (*cp == ' ' || *cp == '\t')
			cp++;
		    cp[strcspn(cp, ";# \t\n")] = '\0';
		    if ((*cp != '\0') && (*cp != '\n')) {
			memset(&hints, 0, sizeof(hints));
			hints.ai_family = PF_UNSPEC;
			hints.ai_socktype = SOCK_DGRAM;	/*dummy*/
			hints.ai_flags = AI_NUMERICHOST;
			sprintf(sbuf, "%u", NAMESERVER_PORT);
			if (getaddrinfo(cp, sbuf, &hints, &ai) == 0) {
			    if (ai->ai_addrlen <= minsiz) {
				if (statp->_u._ext.ext != NULL) {
				    memcpy(&statp->_u._ext.ext->nsaddrs[nserv],
					ai->ai_addr, ai->ai_addrlen);
				}
				if (ai->ai_addrlen <=
				    sizeof(statp->nsaddr_list[nserv])) {
				    memcpy(&statp->nsaddr_list[nserv],
					ai->ai_addr, ai->ai_addrlen);
				} else
				    statp->nsaddr_list[nserv].sin_family = 0;
				nserv++;
			    }
			    freeaddrinfo(ai);
			}
		    }
		    continue;
		}
#ifdef RESOLVSORT
		if (MATCH(buf, "sortlist")) {
		    struct in_addr a;
		    struct in6_addr a6;
		    int m, i;
		    u_char *u;
		    struct __res_state_ext *ext = statp->_u._ext.ext;

		    cp = buf + sizeof("sortlist") - 1;
		    while (nsort < MAXRESOLVSORT) {
			while (*cp == ' ' || *cp == '\t')
			    cp++;
			if (*cp == '\0' || *cp == '\n' || *cp == ';')
			    break;
			net = cp;
			while (*cp && !ISSORTMASK(*cp) && *cp != ';' &&
			       isascii(*cp) && !isspace((unsigned char)*cp))
				cp++;
			n = *cp;
			*cp = 0;
			if (inet_aton(net, &a)) {
			    statp->sort_list[nsort].addr = a;
			    if (ISSORTMASK(n)) {
				*cp++ = n;
				net = cp;
				while (*cp && *cp != ';' &&
					isascii(*cp) &&
					!isspace((unsigned char)*cp))
				    cp++;
				n = *cp;
				*cp = 0;
				if (inet_aton(net, &a)) {
				    statp->sort_list[nsort].mask = a.s_addr;
				} else {
				    statp->sort_list[nsort].mask = 
					net_mask(statp->sort_list[nsort].addr);
				}
			    } else {
				statp->sort_list[nsort].mask = 
				    net_mask(statp->sort_list[nsort].addr);
			    }
			    ext->sort_list[nsort].af = AF_INET;
			    ext->sort_list[nsort].addr.ina =
				statp->sort_list[nsort].addr;
			    ext->sort_list[nsort].mask.ina.s_addr =
				statp->sort_list[nsort].mask;
			    nsort++;
			}
			else if (inet_pton(AF_INET6, net, &a6) == 1) {

			    ext->sort_list[nsort].af = AF_INET6;
			    ext->sort_list[nsort].addr.in6a = a6;
			    u = (u_char *)&ext->sort_list[nsort].mask.in6a;
			    *cp++ = n;
			    net = cp;
			    while (*cp && *cp != ';' &&
				    isascii(*cp) && !isspace(*cp))
				cp++;
			    m = n;
			    n = *cp;
			    *cp = 0;
			    switch (m) {
			    case '/':
				m = atoi(net);
				break;
			    case '&':
				if (inet_pton(AF_INET6, net, u) == 1) {
				    m = -1;
				    break;
				}
				/*FALLTHROUGH*/
			    default:
				m = sizeof(struct in6_addr) * CHAR_BIT;
				break;
			    }
			    if (m >= 0) {
				for (i = 0; i < sizeof(struct in6_addr); i++) {
				    if (m <= 0) {
					*u = 0;
				    } else {
					m -= CHAR_BIT;
					*u = (u_char)~0;
					if (m < 0)
					    *u <<= -m;
				    }
				    u++;
				}
			    }
			    statp->sort_list[nsort].addr.s_addr =
				(u_int32_t)0xffffffff;
			    statp->sort_list[nsort].mask =
				(u_int32_t)0xffffffff;
			    nsort++;
			}
			*cp = n;
		    }
		    continue;
		}
#endif
		if (MATCH(buf, "options")) {
		    res_setoptions(statp, buf + sizeof("options") - 1, "conf");
		    continue;
		}
	    }
	    if (nserv > 0) 
		statp->nscount = nserv;
#ifdef RESOLVSORT
	    statp->nsort = nsort;
#endif
	    (void) fclose(fp);
	}
/*
 * Last chance to get a nameserver.  This should not normally
 * be necessary
 */
#ifdef NO_RESOLV_CONF
	if(nserv == 0)
		nserv = get_nameservers(statp);
#endif

	if (statp->defdname[0] == 0 &&
	    gethostname(buf, sizeof(statp->defdname) - 1) == 0 &&
	    (cp = strchr(buf, '.')) != NULL)
		strcpy(statp->defdname, cp + 1);

	/* find components of local domain that might be searched */
	if (havesearch == 0) {
		pp = statp->dnsrch;
		*pp++ = statp->defdname;
		*pp = NULL;

		dots = 0;
		for (cp = statp->defdname; *cp; cp++)
			dots += (*cp == '.');

		cp = statp->defdname;
		while (pp < statp->dnsrch + MAXDFLSRCH) {
			if (dots < LOCALDOMAINPARTS)
				break;
			cp = strchr(cp, '.') + 1;    /*%< we know there is one */
			*pp++ = cp;
			dots--;
		}
		*pp = NULL;
#ifdef DEBUG
		if (statp->options & RES_DEBUG) {
			printf(";; res_init()... default dnsrch list:\n");
			for (pp = statp->dnsrch; *pp; pp++)
				printf(";;\t%s\n", *pp);
			printf(";;\t..END..\n");
		}
#endif
	}

	if (issetugid())
		statp->options |= RES_NOALIASES;
	else if ((cp = getenv("RES_OPTIONS")) != NULL)
		res_setoptions(statp, cp, "env");
	statp->options |= RES_INIT;
	return (statp->res_h_errno);
}

static void
res_setoptions(res_state statp, const char *options, const char *source)
{
	const char *cp = options;
	int i;
	struct __res_state_ext *ext = statp->_u._ext.ext;

#ifdef DEBUG
	if (statp->options & RES_DEBUG)
		printf(";; res_setoptions(\"%s\", \"%s\")...\n",
		       options, source);
#endif
	while (*cp) {
		/* skip leading and inner runs of spaces */
		while (*cp == ' ' || *cp == '\t')
			cp++;
		/* search for and process individual options */
		if (!strncmp(cp, "ndots:", sizeof("ndots:") - 1)) {
			i = atoi(cp + sizeof("ndots:") - 1);
			if (i <= RES_MAXNDOTS)
				statp->ndots = i;
			else
				statp->ndots = RES_MAXNDOTS;
#ifdef DEBUG
			if (statp->options & RES_DEBUG)
				printf(";;\tndots=%d\n", statp->ndots);
#endif
		} else if (!strncmp(cp, "timeout:", sizeof("timeout:") - 1)) {
			i = atoi(cp + sizeof("timeout:") - 1);
			if (i <= RES_MAXRETRANS)
				statp->retrans = i;
			else
				statp->retrans = RES_MAXRETRANS;
#ifdef DEBUG
			if (statp->options & RES_DEBUG)
				printf(";;\ttimeout=%d\n", statp->retrans);
#endif
#ifdef	SOLARIS2
		} else if (!strncmp(cp, "retrans:", sizeof("retrans:") - 1)) {
			/*
		 	 * For backward compatibility, 'retrans' is
		 	 * supported as an alias for 'timeout', though
		 	 * without an imposed maximum.
		 	 */
			statp->retrans = atoi(cp + sizeof("retrans:") - 1);
		} else if (!strncmp(cp, "retry:", sizeof("retry:") - 1)){
			/*
			 * For backward compatibility, 'retry' is
			 * supported as an alias for 'attempts', though
			 * without an imposed maximum.
			 */
			statp->retry = atoi(cp + sizeof("retry:") - 1);
#endif	/* SOLARIS2 */
		} else if (!strncmp(cp, "attempts:", sizeof("attempts:") - 1)){
			i = atoi(cp + sizeof("attempts:") - 1);
			if (i <= RES_MAXRETRY)
				statp->retry = i;
			else
				statp->retry = RES_MAXRETRY;
#ifdef DEBUG
			if (statp->options & RES_DEBUG)
				printf(";;\tattempts=%d\n", statp->retry);
#endif
		} else if (!strncmp(cp, "debug", sizeof("debug") - 1)) {
#ifdef DEBUG
			if (!(statp->options & RES_DEBUG)) {
				printf(";; res_setoptions(\"%s\", \"%s\")..\n",
				       options, source);
				statp->options |= RES_DEBUG;
			}
			printf(";;\tdebug\n");
#endif
		} else if (!strncmp(cp, "no_tld_query",
				    sizeof("no_tld_query") - 1) ||
			   !strncmp(cp, "no-tld-query",
				    sizeof("no-tld-query") - 1)) {
			statp->options |= RES_NOTLDQUERY;
		} else if (!strncmp(cp, "inet6", sizeof("inet6") - 1)) {
			statp->options |= RES_USE_INET6;
		} else if (!strncmp(cp, "insecure1", sizeof("insecure1") - 1)) {
		       statp->options |= RES_INSECURE1;
		} else if (!strncmp(cp, "insecure2", sizeof("insecure2") - 1)) {
		       statp->options |= RES_INSECURE2;
		} else if (!strncmp(cp, "rotate", sizeof("rotate") - 1)) {
			statp->options |= RES_ROTATE;
		} else if (!strncmp(cp, "usevc", sizeof("usevc") - 1)) {
			statp->options |= RES_USEVC;
		} else if (!strncmp(cp, "no-check-names",
				    sizeof("no-check-names") - 1)) {
			statp->options |= RES_NOCHECKNAME;
		} else if (!strncmp(cp, "reload-period:",
				    sizeof("reload-period:") - 1)) {
			if (ext != NULL) {
				ext->reload_period = (u_short)
				    atoi(cp + sizeof("reload-period:") - 1);
			}
		}
#ifdef RES_USE_EDNS0
		else if (!strncmp(cp, "edns0", sizeof("edns0") - 1)) {
			statp->options |= RES_USE_EDNS0;
		}
#endif
#ifndef _LIBC
		else if (!strncmp(cp, "dname", sizeof("dname") - 1)) {
			statp->options |= RES_USE_DNAME;
		}
		else if (!strncmp(cp, "nibble:", sizeof("nibble:") - 1)) {
			if (ext == NULL)
				goto skip;
			cp += sizeof("nibble:") - 1;
			i = MIN(strcspn(cp, " \t"), sizeof(ext->nsuffix) - 1);
			strncpy(ext->nsuffix, cp, i);
			ext->nsuffix[i] = '\0';
		}
		else if (!strncmp(cp, "nibble2:", sizeof("nibble2:") - 1)) {
			if (ext == NULL)
				goto skip;
			cp += sizeof("nibble2:") - 1;
			i = MIN(strcspn(cp, " \t"), sizeof(ext->nsuffix2) - 1);
			strncpy(ext->nsuffix2, cp, i);
			ext->nsuffix2[i] = '\0';
		}
		else if (!strncmp(cp, "v6revmode:", sizeof("v6revmode:") - 1)) {
			cp += sizeof("v6revmode:") - 1;
			/* "nibble" and "bitstring" used to be valid */
			if (!strncmp(cp, "single", sizeof("single") - 1)) {
				statp->options |= RES_NO_NIBBLE2;
			} else if (!strncmp(cp, "both", sizeof("both") - 1)) {
				statp->options &=
					 ~RES_NO_NIBBLE2;
			}
		}
#endif
		else {
			/* XXX - print a warning here? */
		}
#ifndef _LIBC
   skip:
#endif
		/* skip to next run of spaces */
		while (*cp && *cp != ' ' && *cp != '\t')
			cp++;
	}
}

#ifdef RESOLVSORT
/* XXX - should really support CIDR which means explicit masks always. */
static u_int32_t
net_mask(struct in_addr in)		/*!< XXX - should really use system's version of this  */
{
	u_int32_t i = ntohl(in.s_addr);

	if (IN_CLASSA(i))
		return (htonl(IN_CLASSA_NET));
	else if (IN_CLASSB(i))
		return (htonl(IN_CLASSB_NET));
	return (htonl(IN_CLASSC_NET));
}
#endif

static u_char srnd[16];

void
res_rndinit(res_state statp)
{
	struct timeval now;
	u_int32_t u32;
	u_int16_t u16;
	u_char *rnd = statp->_rnd == NULL ? srnd : statp->_rnd;

	gettimeofday(&now, NULL);
	u32 = now.tv_sec;
	memcpy(rnd, &u32, 4);
	u32 = now.tv_usec;
	memcpy(rnd + 4, &u32, 4);
	u32 += now.tv_sec;
	memcpy(rnd + 8, &u32, 4);
	u16 = getpid();
	memcpy(rnd + 12, &u16, 2);
}

u_int
res_nrandomid(res_state statp) {
	struct timeval now;
	u_int16_t u16;
	MD5_CTX ctx;
	u_char *rnd = statp->_rnd == NULL ? srnd : statp->_rnd;

	gettimeofday(&now, NULL);
	u16 = (u_int16_t) (now.tv_sec ^ now.tv_usec);
	memcpy(rnd + 14, &u16, 2);
#ifndef HAVE_MD5
	MD5_Init(&ctx);
	MD5_Update(&ctx, rnd, 16);
	MD5_Final(rnd, &ctx);
#else
	MD5Init(&ctx);
	MD5Update(&ctx, rnd, 16);
	MD5Final(rnd, &ctx);
#endif
	memcpy(&u16, rnd + 14, 2);
	return ((u_int) u16);
}

/*%
 * This routine is for closing the socket if a virtual circuit is used and
 * the program wants to close it.  This provides support for endhostent()
 * which expects to close the socket.
 *
 * This routine is not expected to be user visible.
 */
void
res_nclose(res_state statp) {
	int ns;

	if (statp->_vcsock >= 0) { 
		(void) _close(statp->_vcsock);
		statp->_vcsock = -1;
		statp->_flags &= ~(RES_F_VC | RES_F_CONN);
	}
	for (ns = 0; ns < statp->_u._ext.nscount; ns++) {
		if (statp->_u._ext.nssocks[ns] != -1) {
			(void) _close(statp->_u._ext.nssocks[ns]);
			statp->_u._ext.nssocks[ns] = -1;
		}
	}
}

void
res_ndestroy(res_state statp) {
	res_nclose(statp);
	if (statp->_u._ext.ext != NULL) {
		free(statp->_u._ext.ext);
		statp->_u._ext.ext = NULL;
	}
	if (statp->_rnd != NULL) {
		free(statp->_rnd);
		statp->_rnd = NULL;
	}
	statp->options &= ~RES_INIT;
}

#ifndef _LIBC
const char *
res_get_nibblesuffix(res_state statp) {
	if (statp->_u._ext.ext)
		return (statp->_u._ext.ext->nsuffix);
	return ("ip6.arpa");
}

const char *
res_get_nibblesuffix2(res_state statp) {
	if (statp->_u._ext.ext)
		return (statp->_u._ext.ext->nsuffix2);
	return ("ip6.int");
}
#endif

void
res_setservers(res_state statp, const union res_sockaddr_union *set, int cnt) {
	int i, nserv;
	size_t size;

	/* close open servers */
	res_nclose(statp);

	/* cause rtt times to be forgotten */
	statp->_u._ext.nscount = 0;

	nserv = 0;
	for (i = 0; i < cnt && nserv < MAXNS; i++) {
		switch (set->sin.sin_family) {
		case AF_INET:
			size = sizeof(set->sin);
			if (statp->_u._ext.ext)
				memcpy(&statp->_u._ext.ext->nsaddrs[nserv],
					&set->sin, size);
			if (size <= sizeof(statp->nsaddr_list[nserv]))
				memcpy(&statp->nsaddr_list[nserv],
					&set->sin, size);
			else
				statp->nsaddr_list[nserv].sin_family = 0;
			nserv++;
			break;

#ifdef HAS_INET6_STRUCTS
		case AF_INET6:
			size = sizeof(set->sin6);
			if (statp->_u._ext.ext)
				memcpy(&statp->_u._ext.ext->nsaddrs[nserv],
					&set->sin6, size);
			if (size <= sizeof(statp->nsaddr_list[nserv]))
				memcpy(&statp->nsaddr_list[nserv],
					&set->sin6, size);
			else
				statp->nsaddr_list[nserv].sin_family = 0;
			nserv++;
			break;
#endif

		default:
			break;
		}
		set++;
	}
	statp->nscount = nserv;
	
}

int
res_getservers(res_state statp, union res_sockaddr_union *set, int cnt) {
	int i;
	size_t size;
	u_int16_t family;

	for (i = 0; i < statp->nscount && i < cnt; i++) {
		if (statp->_u._ext.ext)
			family = statp->_u._ext.ext->nsaddrs[i].sin.sin_family;
		else 
			family = statp->nsaddr_list[i].sin_family;

		switch (family) {
		case AF_INET:
			size = sizeof(set->sin);
			if (statp->_u._ext.ext)
				memcpy(&set->sin,
				       &statp->_u._ext.ext->nsaddrs[i],
				       size);
			else
				memcpy(&set->sin, &statp->nsaddr_list[i],
				       size);
			break;

#ifdef HAS_INET6_STRUCTS
		case AF_INET6:
			size = sizeof(set->sin6);
			if (statp->_u._ext.ext)
				memcpy(&set->sin6,
				       &statp->_u._ext.ext->nsaddrs[i],
				       size);
			else
				memcpy(&set->sin6, &statp->nsaddr_list[i],
				       size);
			break;
#endif

		default:
			set->sin.sin_family = 0;
			break;
		}
		set++;
	}
	return (statp->nscount);
}

/*! \file */
