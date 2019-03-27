/*-
 * SPDX-License-Identifier: BSD-4-Clause
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stdlib.h>
#include <rpc/rpc.h>
#include <rpcsvc/yp.h>
#include <rpcsvc/yppasswd.h>
#include <rpcsvc/ypxfrd.h>
#include <sys/types.h>
#include <limits.h>
#include <db.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <paths.h>
#include <errno.h>
#include <sys/param.h>
#include "yp_extern.h"
#ifdef TCP_WRAPPER
#include "tcpd.h"
#endif

extern int debug;

static const char *yp_procs[] = {
	/* NIS v1 */
	"ypoldproc_null",
	"ypoldproc_domain",
	"ypoldproc_domain_nonack",
	"ypoldproc_match",
	"ypoldproc_first",
	"ypoldproc_next",
	"ypoldproc_poll",
	"ypoldproc_push",
	"ypoldproc_get",
	"badproc1", /* placeholder */
	"badproc2", /* placeholder */
	"badproc3", /* placeholder */

	/* NIS v2 */
	"ypproc_null",
	"ypproc_domain",
	"ypproc_domain_nonack",
	"ypproc_match",
	"ypproc_first",
	"ypproc_next",
	"ypproc_xfr",
	"ypproc_clear",
	"ypproc_all",
	"ypproc_master",
	"ypproc_order",
	"ypproc_maplist"
};

struct securenet {
	struct in_addr net;
	struct in_addr mask;
	struct securenet *next;
};

static struct securenet *securenets;

#define LINEBUFSZ 1024

#ifdef TCP_WRAPPER
int hosts_ctl(char *, char *, char *, char *);
#endif

/*
 * Read /var/yp/securenets file and initialize the securenets
 * list. If the file doesn't exist, we set up a dummy entry that
 * allows all hosts to connect.
 */
void
load_securenets(void)
{
	FILE *fp;
	char path[MAXPATHLEN + 2];
	char linebuf[1024 + 2];
	struct securenet *tmp;

	/*
	 * If securenets is not NULL, we are being called to reload
	 * the list; free the existing list before re-reading the
	 * securenets file.
	 */
	while (securenets) {
		tmp = securenets->next;
		free(securenets);
		securenets = tmp;
	}

	snprintf(path, MAXPATHLEN, "%s/securenets", yp_dir);

	if ((fp = fopen(path, "r")) == NULL) {
		if (errno == ENOENT) {
			securenets = malloc(sizeof(struct securenet));
			securenets->net.s_addr = INADDR_ANY;
			securenets->mask.s_addr = INADDR_ANY;
			securenets->next = NULL;
			return;
		} else {
			yp_error("fopen(%s) failed: %s", path, strerror(errno));
			exit(1);
		}
	}

	securenets = NULL;

	while (fgets(linebuf, LINEBUFSZ, fp)) {
		char addr1[20], addr2[20];

		if ((linebuf[0] == '#')
		    || (strspn(linebuf, " \t\r\n") == strlen(linebuf)))
			continue;
		if (sscanf(linebuf, "%s %s", addr1, addr2) < 2) {
			yp_error("badly formatted securenets entry: %s",
							linebuf);
			continue;
		}

		tmp = malloc(sizeof(struct securenet));

		if (!inet_aton((char *)&addr1, (struct in_addr *)&tmp->net)) {
			yp_error("badly formatted securenets entry: %s", addr1);
			free(tmp);
			continue;
		}

		if (!inet_aton((char *)&addr2, (struct in_addr *)&tmp->mask)) {
			yp_error("badly formatted securenets entry: %s", addr2);
			free(tmp);
			continue;
		}

		tmp->next = securenets;
		securenets = tmp;
	}

	fclose(fp);

}

/*
 * Access control functions.
 *
 * yp_access() checks the mapname and client host address and watches for
 * the following things:
 *
 * - If the client is referencing one of the master.passwd.* or shadow.* maps,
 *   it must be using a privileged port to make its RPC to us. If it is, then
 *   we can assume that the caller is root and allow the RPC to succeed. If it
 *   isn't access is denied.
 *
 * - The client's IP address is checked against the securenets rules.
 *   There are two kinds of securenets support: the built-in support,
 *   which is very simple and depends on the presence of a
 *   /var/yp/securenets file, and tcp-wrapper support, which requires
 *   Wietse Venema's libwrap.a and tcpd.h. (Since the tcp-wrapper
 *   package does not ship with FreeBSD, we use the built-in support
 *   by default. Users can recompile the server with the tcp-wrapper library
 *   if they already have it installed and want to use hosts.allow and
 *   hosts.deny to control access instead of having a separate securenets
 *   file.)
 *
 *   If no /var/yp/securenets file is present, the host access checks
 *   are bypassed and all hosts are allowed to connect.
 *
 * The yp_validdomain() function checks the domain specified by the caller
 * to make sure it's actually served by this server. This is more a sanity
 * check than an a security check, but this seems to be the best place for
 * it.
 */

#ifdef DB_CACHE
int
yp_access(const char *map, const char *domain, const struct svc_req *rqstp)
#else
int
yp_access(const char *map, const struct svc_req *rqstp)
#endif
{
	struct sockaddr_in *rqhost;
	int status_securenets = 0;
#ifdef TCP_WRAPPER
	int status_tcpwrap;
#endif
	static unsigned long oldaddr = 0;
	struct securenet *tmp;
	const char *yp_procedure = NULL;
	char procbuf[50];

	if (rqstp->rq_prog != YPPASSWDPROG && rqstp->rq_prog != YPPROG) {
		snprintf(procbuf, sizeof(procbuf), "#%lu/#%lu",
		    (unsigned long)rqstp->rq_prog,
		    (unsigned long)rqstp->rq_proc);
		yp_procedure = (char *)&procbuf;
	} else {
		yp_procedure = rqstp->rq_prog == YPPASSWDPROG ?
		"yppasswdprog_update" :
		yp_procs[rqstp->rq_proc + (12 * (rqstp->rq_vers - 1))];
	}

	rqhost = svc_getcaller(rqstp->rq_xprt);

	if (debug) {
		yp_error("procedure %s called from %s:%d", yp_procedure,
			inet_ntoa(rqhost->sin_addr),
			ntohs(rqhost->sin_port));
		if (map != NULL)
			yp_error("client is referencing map \"%s\".", map);
	}

	/* Check the map name if one was supplied. */
	if (map != NULL) {
		if (strchr(map, '/')) {
			yp_error("embedded slash in map name \"%s\" -- \
possible spoof attempt from %s:%d",
				map, inet_ntoa(rqhost->sin_addr),
				ntohs(rqhost->sin_port));
			return(1);
		}
#ifdef DB_CACHE
		if ((yp_testflag((char *)map, (char *)domain, YP_SECURE) ||
#else
		if ((strstr(map, "master.passwd.") || strstr(map, "shadow.") ||
#endif
		    (rqstp->rq_prog == YPPROG &&
		     rqstp->rq_proc == YPPROC_XFR) ||
		    (rqstp->rq_prog == YPXFRD_FREEBSD_PROG &&
		     rqstp->rq_proc == YPXFRD_GETMAP)) &&
		     ntohs(rqhost->sin_port) >= IPPORT_RESERVED) {
			yp_error("access to %s denied -- client %s:%d \
not privileged", map, inet_ntoa(rqhost->sin_addr), ntohs(rqhost->sin_port));
			return(1);
		}
	}

#ifdef TCP_WRAPPER
	status_tcpwrap = hosts_ctl("ypserv", STRING_UNKNOWN,
			   inet_ntoa(rqhost->sin_addr), "");
#endif
	tmp = securenets;
	while (tmp) {
		if (((rqhost->sin_addr.s_addr & ~tmp->mask.s_addr)
		    | tmp->net.s_addr) == rqhost->sin_addr.s_addr) {
			status_securenets = 1;
			break;
		}
		tmp = tmp->next;
	}

#ifdef TCP_WRAPPER
	if (status_securenets == 0 || status_tcpwrap == 0) {
#else
	if (status_securenets == 0) {
#endif
	/*
	 * One of the following two events occurred:
	 *
	 * (1) The /var/yp/securenets exists and the remote host does not
	 *     match any of the networks specified in it.
	 * (2) The hosts.allow file has denied access and TCP_WRAPPER is
	 *     defined.
	 *
	 * In either case deny access.
	 */
		if (rqhost->sin_addr.s_addr != oldaddr) {
			yp_error("connect from %s:%d to procedure %s refused",
					inet_ntoa(rqhost->sin_addr),
					ntohs(rqhost->sin_port),
					yp_procedure);
			oldaddr = rqhost->sin_addr.s_addr;
		}
		return(1);
	}
	return(0);

}

int
yp_validdomain(const char *domain)
{
	struct stat statbuf;
	char dompath[MAXPATHLEN + 2];

	if (domain == NULL || strstr(domain, "binding") ||
	    !strcmp(domain, ".") || !strcmp(domain, "..") ||
	    strchr(domain, '/') || strlen(domain) > YPMAXDOMAIN)
		return(1);

	snprintf(dompath, sizeof(dompath), "%s/%s", yp_dir, domain);

	if (stat(dompath, &statbuf) < 0 || !S_ISDIR(statbuf.st_mode))
		return(1);


	return(0);
}
