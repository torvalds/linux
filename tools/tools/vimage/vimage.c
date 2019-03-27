/*
 * Copyright (c) 2002-2004 Marko Zec <zec@fer.hr>
 * Copyright (c) 2009 University of Zagreb
 * Copyright (c) 2009 FreeBSD Foundation
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
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/jail.h>
#include <sys/socket.h>

#include <net/if.h>

#include <ctype.h>
#include <jail.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef enum {
	VI_SWITCHTO,
	VI_CREATE,
	VI_MODIFY,
	VI_DESTROY,
	VI_IFMOVE,
	VI_GET
} vi_cmd_t;

typedef struct vimage_status {
	char name[MAXPATHLEN];		/* Must be first field for strcmp(). */
	char path[MAXPATHLEN];
	char hostname[MAXPATHLEN];
	char domainname[MAXPATHLEN];
	int jid;
	int parentjid;
	int vnet;
	int childcnt;
	int childmax;
	int cpuset;
	int rawsock;
	int socket_af;
	int mount;
} vstat_t;

#define	VST_SIZE_STEP	1024
#define	MAXPARAMS	32

static int getjail(vstat_t *, int, int);

static char *invocname;

static void
usage(void)
{

	fprintf(stderr,
	    "usage: %s [-c | -m] vname [param=value ...]\n"
	    "       %s -d vname\n"
	    "       %s -l[rvj] [vname]\n"
	    "       %s -i vname ifname [newifname]\n"
	    "       %s vname [command ...]\n",
	    invocname, invocname, invocname, invocname, invocname);
	exit(1);
}

int
main(int argc, char **argv)
{
	struct jailparam params[MAXPARAMS];
	char ifname[IFNAMSIZ];
	struct ifreq ifreq;
	vi_cmd_t newcmd, cmd;
	int recurse = 0;
	int verbose = 0;
	int jid, i, s, namelen;
	int vst_size, vst_last;
	vstat_t *vst;
	char *str;
	char ch;

	invocname = argv[0];

	newcmd = cmd = VI_SWITCHTO; /* Default if no modifiers specified. */
	while ((ch = getopt(argc, argv, "cdijlmrv")) != -1) {
		switch (ch) {
		case 'c':
			newcmd = VI_CREATE;
			break;
		case 'm':
			newcmd = VI_MODIFY;
			break;
		case 'd':
			newcmd = VI_DESTROY;
			break;
		case 'l':
			newcmd = VI_GET;
			break;
		case 'i':
			newcmd = VI_IFMOVE;
			break;
		case 'r':
			recurse = 1;
			break;
		case 'v':
			verbose++;
			break;
		case 'j':
			verbose = 2;
			break;
		default:
			usage();
		}
		if (cmd == VI_SWITCHTO || cmd == newcmd)
			cmd = newcmd;
		else
			usage();
	}
	argc -= optind;
	argv += optind;

	if ((cmd != VI_GET && (argc == 0 || recurse != 0 || verbose != 0)) ||
	    (cmd == VI_IFMOVE && (argc < 2 || argc > 3)) ||
	    (cmd == VI_MODIFY && argc < 2) || argc >= MAXPARAMS)
		usage();

	switch (cmd) {
	case VI_GET:
		vst_last = 0;
		vst_size = VST_SIZE_STEP;
		if ((vst = malloc(vst_size * sizeof(*vst))) == NULL)
			break;
		if (argc == 1)
			namelen = strlen(argv[0]);
		else
			namelen = 0;
		jid = 0;
		while ((jid = getjail(&vst[vst_last], jid, verbose)) > 0) {
			/* Skip jails which do not own vnets. */
			if (vst[vst_last].vnet != 1)
				continue;
			/* Skip non-matching vnames / hierarchies. */
			if (namelen &&
			    ((strlen(vst[vst_last].name) < namelen ||
			    strncmp(vst[vst_last].name, argv[0], namelen) != 0)
			    || (strlen(vst[vst_last].name) > namelen &&
			    vst[vst_last].name[namelen] != '.')))
				continue;
			/* Skip any sub-trees if -r not requested. */
			if (!recurse &&
			    (strlen(vst[vst_last].name) < namelen ||
			    strchr(&vst[vst_last].name[namelen], '.') != NULL))
				continue;
			/* Grow vst table if necessary. */
			if (++vst_last == vst_size) {
				vst_size += VST_SIZE_STEP;
				vst = realloc(vst, vst_size * sizeof(*vst));
				if (vst == NULL)
					break;
			}
		}
		if (vst == NULL)
			break;
		/* Sort: the key is the 1st field in *vst, i.e. vimage name. */
		qsort(vst, vst_last, sizeof(*vst), (void *) strcmp);
		for (i = 0; i < vst_last; i++) {
			if (!verbose) {
				printf("%s\n", vst[i].name);
				continue;
			}

			printf("%s:\n", vst[i].name);
			printf("    Path: %s\n", vst[i].path);
			printf("    Hostname: %s\n", vst[i].hostname);
			printf("    Domainname: %s\n", vst[i].domainname);
			printf("    Children: %d\n", vst[i].childcnt);

			if (verbose < 2)
				continue;

			printf("    Children limit: %d\n", vst[i].childmax);
			printf("    CPUsetID: %d\n", vst[i].cpuset);
			printf("    JID: %d\n", vst[i].jid);
			printf("    PJID: %d\n", vst[i].parentjid);
			printf("    Raw sockets allowed: %d\n", vst[i].rawsock);
			printf("    All AF allowed: %d\n", vst[i].socket_af);
			printf("    Mount allowed: %d\n", vst[i].mount);
		}
		free(vst);
		exit(0);

	case VI_IFMOVE:
		if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
			break;
		if ((jid = jail_getid(argv[0])) < 0)
			break;
		ifreq.ifr_jid = jid;
		strncpy(ifreq.ifr_name, argv[1], sizeof(ifreq.ifr_name));
		if (ioctl(s, SIOCSIFVNET, (caddr_t)&ifreq) < 0)
			break;
		close(s);
		if (argc == 3)
			snprintf(ifname, sizeof(ifname), "%s", argv[2]);
		else
			snprintf(ifname, sizeof(ifname), "eth0");
		ifreq.ifr_data = ifname;
		/* Do we need to rename the ifnet? */
		if (strcmp(ifreq.ifr_name, ifname) != 0) {
			/* Switch to the context of the target vimage. */
			if (jail_attach(jid) < 0)
				break;
			if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
				break;
			for (namelen = 0; isalpha(ifname[namelen]); namelen++);
			i = 0;
			/* Search for a free ifunit in target vnet.  Unsafe. */
			while (ioctl(s, SIOCSIFNAME, (caddr_t)&ifreq) < 0) {
				snprintf(&ifname[namelen],
				    sizeof(ifname) - namelen, "%d", i);
				/* Emergency brake. */
				if (i++ == IF_MAXUNIT)
					break;
			}
		}
		if (i < IF_MAXUNIT)
			printf("%s@%s\n", ifname, argv[0]);
		else
			printf("%s@%s\n", ifreq.ifr_name, argv[0]);
		exit(0);

	case VI_CREATE:
		if (jail_setv(JAIL_CREATE,
		    "name", argv[0],
		    "vnet", NULL,
		    "host", NULL,
		    "persist", NULL,
		    "allow.raw_sockets", "true",
		    "allow.socket_af", "true",
		    "allow.mount", "true",
		    NULL) < 0)
			break;
		if (argc == 1)
			exit(0);
		/* Not done yet, proceed to apply non-default parameters. */

	case VI_MODIFY:
		jailparam_init(&params[0], "name");
		jailparam_import(&params[0], argv[0]);
		for (i = 1; i < argc; i++) {
			for (str = argv[i]; *str != '=' && *str != 0; str++) {
				/* Do nothing - search for '=' delimeter. */
			}
			if (*str == 0)
				break;
			*str++ = 0;
			if (*str == 0)
				break;
			jailparam_init(&params[i], argv[i]);
			jailparam_import(&params[i], str);
		}
		if (i != argc)
			break;
		if (jailparam_set(params, i, JAIL_UPDATE) < 0)
			break;
		exit(0);

	case VI_DESTROY:
		if ((jid = jail_getid(argv[0])) < 0)
			break;
		if (jail_remove(jid) < 0)
			break;
		exit(0);

	case VI_SWITCHTO:
		if ((jid = jail_getid(argv[0])) < 0)
			break;
		if (jail_attach(jid) < 0)
			break;
		if (argc == 1) {
			printf("Switched to vimage %s\n", argv[0]);
			if ((str = getenv("SHELL")) == NULL)
				execlp("/bin/sh", invocname, NULL);
			else
				execlp(str, invocname, NULL);
		} else 
			execvp(argv[1], &argv[1]);
		break;

	default:
		/* Should be unreachable. */
		break;
	}

	if (jail_errmsg[0])
		fprintf(stderr, "Error: %s\n", jail_errmsg);
	else
		perror("Error");
	exit(1);
}

static int
getjail(vstat_t *vs, int lastjid, int verbose)
{
	struct jailparam params[32];	/* Must be > max(psize). */
	int psize = 0;

	bzero(params, sizeof(params));
	bzero(vs, sizeof(*vs));

	jailparam_init(&params[psize], "lastjid");
	jailparam_import_raw(&params[psize++], &lastjid, sizeof lastjid);

	jailparam_init(&params[psize], "vnet");
	jailparam_import_raw(&params[psize++], &vs->vnet, sizeof(vs->vnet));

	jailparam_init(&params[psize], "name");
	jailparam_import_raw(&params[psize++], &vs->name, sizeof(vs->name));

	if (verbose == 0)
		goto done;

	jailparam_init(&params[psize], "path");
	jailparam_import_raw(&params[psize++], &vs->path, sizeof(vs->path));

	jailparam_init(&params[psize], "host.hostname");
	jailparam_import_raw(&params[psize++], &vs->hostname,
	    sizeof(vs->hostname));

	jailparam_init(&params[psize], "host.domainname");
	jailparam_import_raw(&params[psize++], &vs->domainname,
	    sizeof(vs->domainname));

	jailparam_init(&params[psize], "children.cur");
	jailparam_import_raw(&params[psize++], &vs->childcnt,
	    sizeof(vs->childcnt));

	if (verbose == 1)
		goto done;

	jailparam_init(&params[psize], "children.max");
	jailparam_import_raw(&params[psize++], &vs->childmax,
	    sizeof(vs->childmax));

	jailparam_init(&params[psize], "cpuset.id");
	jailparam_import_raw(&params[psize++], &vs->cpuset,
	    sizeof(vs->cpuset));

	jailparam_init(&params[psize], "parent");
	jailparam_import_raw(&params[psize++], &vs->parentjid,
	    sizeof(vs->parentjid));

	jailparam_init(&params[psize], "allow.raw_sockets");
	jailparam_import_raw(&params[psize++], &vs->rawsock,
	    sizeof(vs->rawsock));

	jailparam_init(&params[psize], "allow.socket_af");
	jailparam_import_raw(&params[psize++], &vs->socket_af,
	    sizeof(vs->socket_af));

	jailparam_init(&params[psize], "allow.mount");
	jailparam_import_raw(&params[psize++], &vs->mount, sizeof(vs->mount));

done:
	vs->jid = jailparam_get(params, psize, 0);
	jailparam_free(params, psize);
	return (vs->jid);
}
