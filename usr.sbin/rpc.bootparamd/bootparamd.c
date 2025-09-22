/*	$OpenBSD: bootparamd.c,v 1.23 2024/08/21 14:59:49 florian Exp $	*/

/*
 * This code is not copyright, and is placed in the public domain.
 * Feel free to use and modify. Please send modifications and/or
 * suggestions + bug fixes to Klas Heggemann <klas@nada.kth.se>
 *
 * Various small changes by Theo de Raadt <deraadt@fsa.ca>
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/socket.h>

#include <rpc/rpc.h>
#include <rpcsvc/bootparam_prot.h>
#include <rpcsvc/ypclnt.h>
#include <rpcsvc/yp_prot.h>
#include <arpa/inet.h>

#include <stdio.h>
#include <netdb.h>
#include <ctype.h>
#include <syslog.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <stdlib.h>

#include "pathnames.h"

#define MAXLEN 800

struct hostent *he;
static char hostname[MAX_MACHINE_NAME];
static char askname[MAX_MACHINE_NAME];
static char domain_name[MAX_MACHINE_NAME];

extern void bootparamprog_1(struct svc_req *, SVCXPRT *);
int lookup_bootparam(char *client, char *client_canonical, char *id,
    char **server, char **path);

int	_rpcsvcdirty = 0;
int	_rpcpmstart = 0;
int	debug = 0;
int	dolog = 0;
struct in_addr route_addr;
struct sockaddr_in my_addr;
extern char *__progname;
char   *bootpfile = _PATH_BOOTPARAMS;


static void
usage(void)
{
	extern char *__progname;
	fprintf(stderr, "usage: %s [-ds] [-f file] [-r router]\n",
	    __progname);
	exit(1);
}

/*
 * ever familiar
 */
int
main(int argc, char *argv[])
{
	struct addrinfo hints, *res;
	struct stat buf;
	SVCXPRT *transp;
	int    c;

	while ((c = getopt(argc, argv, "dsr:f:")) != -1)
		switch (c) {
		case 'd':
			debug = 1;
			break;
		case 'r':
			memset(&hints, 0, sizeof(hints));
			hints.ai_family = AF_INET;

			if (getaddrinfo(optarg, NULL, &hints, &res) != 0) {
				warnx("no such host: %s", optarg);
				usage();
			}
			route_addr =
			    ((struct sockaddr_in *)res->ai_addr)->sin_addr;
			freeaddrinfo(res);
			break;
		case 'f':
			bootpfile = optarg;
			break;
		case 's':
			dolog = 1;
#ifndef LOG_DAEMON
			openlog(__progname, 0, 0);
#else
			openlog(__progname, 0, LOG_DAEMON);
			setlogmask(LOG_UPTO(LOG_NOTICE));
#endif
			break;
		default:
			usage();
		}

	if (stat(bootpfile, &buf))
		err(1, "%s", bootpfile);

	if (!route_addr.s_addr) {
		get_myaddress(&my_addr);
		bcopy(&my_addr.sin_addr.s_addr, &route_addr.s_addr,
		    sizeof(route_addr.s_addr));
	}
	if (!debug) {
		if (daemon(0, 0))
			err(1, "can't detach from terminal");
	}

	(void) pmap_unset(BOOTPARAMPROG, BOOTPARAMVERS);

	transp = svcudp_create(RPC_ANYSOCK);
	if (transp == NULL)
		errx(1, "can't create udp service");

	if (!svc_register(transp, BOOTPARAMPROG, BOOTPARAMVERS, bootparamprog_1,
	    IPPROTO_UDP))
		errx(1, "unable to register BOOTPARAMPROG version %ld, udp",
		    BOOTPARAMVERS);

	if (pledge("stdio rpath dns", NULL) == -1)
		err(1, "pledge");

	svc_run();
	errx(1, "svc_run returned");
}

bp_whoami_res *
bootparamproc_whoami_1_svc(bp_whoami_arg *whoami, struct svc_req *rqstp)
{
	in_addr_t haddr;
	static bp_whoami_res res;

	if (debug)
		warnx("whoami got question for %d.%d.%d.%d",
		    255 & whoami->client_address.bp_address_u.ip_addr.net,
		    255 & whoami->client_address.bp_address_u.ip_addr.host,
		    255 & whoami->client_address.bp_address_u.ip_addr.lh,
		    255 & whoami->client_address.bp_address_u.ip_addr.impno);
	if (dolog)
		syslog(LOG_NOTICE, "whoami got question for %d.%d.%d.%d",
		    255 & whoami->client_address.bp_address_u.ip_addr.net,
		    255 & whoami->client_address.bp_address_u.ip_addr.host,
		    255 & whoami->client_address.bp_address_u.ip_addr.lh,
		    255 & whoami->client_address.bp_address_u.ip_addr.impno);

	bcopy(&whoami->client_address.bp_address_u.ip_addr,
	    &haddr, sizeof(haddr));
	he = gethostbyaddr(&haddr, sizeof(haddr), AF_INET);
	if (!he)
		goto failed;

	if (debug)
		warnx("This is host %s", he->h_name);
	if (dolog)
		syslog(LOG_NOTICE, "This is host %s", he->h_name);

	strlcpy(askname, he->h_name, sizeof askname);
	if (!lookup_bootparam(askname, hostname, NULL, NULL, NULL)) {
		res.client_name = hostname;
		getdomainname(domain_name, MAX_MACHINE_NAME);
		res.domain_name = domain_name;

		if (res.router_address.address_type != IP_ADDR_TYPE) {
			res.router_address.address_type = IP_ADDR_TYPE;
			bcopy(&route_addr.s_addr,
			    &res.router_address.bp_address_u.ip_addr, 4);
		}
		if (debug)
			warnx("Returning %s   %s    %d.%d.%d.%d",
			    res.client_name, res.domain_name,
			    255 & res.router_address.bp_address_u.ip_addr.net,
			    255 & res.router_address.bp_address_u.ip_addr.host,
			    255 & res.router_address.bp_address_u.ip_addr.lh,
			    255 & res.router_address.bp_address_u.ip_addr.impno);
		if (dolog)
			syslog(LOG_NOTICE, "Returning %s   %s    %d.%d.%d.%d",
			    res.client_name, res.domain_name,
			    255 & res.router_address.bp_address_u.ip_addr.net,
			    255 & res.router_address.bp_address_u.ip_addr.host,
			    255 & res.router_address.bp_address_u.ip_addr.lh,
			    255 & res.router_address.bp_address_u.ip_addr.impno);
		return (&res);
	}
failed:
	if (debug)
		warnx("whoami failed");
	if (dolog)
		syslog(LOG_NOTICE, "whoami failed");
	return (NULL);
}


bp_getfile_res *
bootparamproc_getfile_1_svc(bp_getfile_arg *getfile, struct svc_req *rqstp)
{
	static bp_getfile_res res;
	int error;

	if (debug)
		warnx("getfile got question for \"%s\" and file \"%s\"",
		    getfile->client_name, getfile->file_id);

	if (dolog)
		syslog(LOG_NOTICE,
		    "getfile got question for \"%s\" and file \"%s\"",
		    getfile->client_name, getfile->file_id);

	he = NULL;
	he = gethostbyname(getfile->client_name);
	if (!he)
		goto failed;

	strlcpy(askname, he->h_name, sizeof askname);
	error = lookup_bootparam(askname, NULL, getfile->file_id,
	    &res.server_name, &res.server_path);
	if (error == 0) {
		he = gethostbyname(res.server_name);
		if (!he)
			goto failed;
		bcopy(he->h_addr, &res.server_address.bp_address_u.ip_addr, 4);
		res.server_address.address_type = IP_ADDR_TYPE;
	} else if (error == ENOENT && !strcmp(getfile->file_id, "dump")) {
		/* Special for dump, answer with null strings. */
		res.server_name[0] = '\0';
		res.server_path[0] = '\0';
		bzero(&res.server_address.bp_address_u.ip_addr, 4);
	} else {
failed:
		if (debug)
			warnx("getfile failed for %s", getfile->client_name);
		if (dolog)
			syslog(LOG_NOTICE,
			    "getfile failed for %s", getfile->client_name);
		return (NULL);
	}

	if (debug)
		warnx("returning server:%s path:%s address: %d.%d.%d.%d",
		    res.server_name, res.server_path,
		    255 & res.server_address.bp_address_u.ip_addr.net,
		    255 & res.server_address.bp_address_u.ip_addr.host,
		    255 & res.server_address.bp_address_u.ip_addr.lh,
		    255 & res.server_address.bp_address_u.ip_addr.impno);
	if (dolog)
		syslog(LOG_NOTICE,
		    "returning server:%s path:%s address: %d.%d.%d.%d",
		    res.server_name, res.server_path,
		    255 & res.server_address.bp_address_u.ip_addr.net,
		    255 & res.server_address.bp_address_u.ip_addr.host,
		    255 & res.server_address.bp_address_u.ip_addr.lh,
		    255 & res.server_address.bp_address_u.ip_addr.impno);
	return (&res);
}

int
lookup_bootparam(char *client, char *client_canonical, char *id,
    char **server, char **path)
{
	FILE   *f;
	static char buf[BUFSIZ];
	char   *bp, *word = NULL;
	size_t  idlen = id == NULL ? 0 : strlen(id);
	int	contin = 0, found = 0;

	f = fopen(bootpfile, "r");
	if (f == NULL)
		return EINVAL;	/* ? */

	while (fgets(buf, sizeof buf, f)) {
		int	wascontin = contin;

		contin = buf[strlen(buf) - 2] == '\\';
		bp = buf + strspn(buf, " \t\n");

		switch (wascontin) {
		case -1:
			/* Continuation of uninteresting line */
			contin *= -1;
			continue;
		case 0:
			/* New line */
			contin *= -1;
			if (*bp == '#')
				continue;
			if ((word = strsep(&bp, " \t\n")) == NULL)
				continue;
			/* See if this line's client is the one we are
			 * looking for */
			if (strcasecmp(word, client) != 0) {
				/*
				 * If it didn't match, try getting the
				 * canonical host name of the client
				 * on this line and comparing that to
				 * the client we are looking for
				 */
				struct hostent *hp = gethostbyname(word);
				if (hp == NULL || strcasecmp(hp->h_name, client))
					continue;
			}
			contin *= -1;
			break;
		case 1:
			/* Continued line we want to parse below */
			break;
		}

		if (client_canonical)
			strlcpy(client_canonical, word, MAX_MACHINE_NAME);

		/* We have found a line for CLIENT */
		if (id == NULL) {
			(void) fclose(f);
			return 0;
		}

		/* Look for a value for the parameter named by ID */
		while ((word = strsep(&bp, " \t\n")) != NULL) {
			if (!strncmp(word, id, idlen) && word[idlen] == '=') {
				/* We have found the entry we want */
				*server = &word[idlen + 1];
				*path = strchr(*server, ':');
				if (*path == NULL)
					/* Malformed entry */
					continue;
				*(*path)++ = '\0';
				(void) fclose(f);
				return 0;
			}
		}

		found = 1;
	}

	(void) fclose(f);
	return found ? ENOENT : EPERM;
}
