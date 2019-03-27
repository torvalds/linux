
/*
 * show.c
 *
 * Copyright (c) 1996-1999 Whistle Communications, Inc.
 * All rights reserved.
 * 
 * Subject to the following obligations and disclaimer of warranty, use and
 * redistribution of this software, in source or object code forms, with or
 * without modifications are expressly permitted by Whistle Communications;
 * provided, however, that:
 * 1. Any and all reproductions of the source or object code must include the
 *    copyright notice above and the following disclaimer of warranties; and
 * 2. No rights are granted, in any manner or form, to use Whistle
 *    Communications, Inc. trademarks, including the mark "WHISTLE
 *    COMMUNICATIONS" on advertising, endorsements, or otherwise except as
 *    such appears in the above copyright notice or in the software.
 * 
 * THIS SOFTWARE IS BEING PROVIDED BY WHISTLE COMMUNICATIONS "AS IS", AND
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, WHISTLE COMMUNICATIONS MAKES NO
 * REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED, REGARDING THIS SOFTWARE,
 * INCLUDING WITHOUT LIMITATION, ANY AND ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT.
 * WHISTLE COMMUNICATIONS DOES NOT WARRANT, GUARANTEE, OR MAKE ANY
 * REPRESENTATIONS REGARDING THE USE OF, OR THE RESULTS OF THE USE OF THIS
 * SOFTWARE IN TERMS OF ITS CORRECTNESS, ACCURACY, RELIABILITY OR OTHERWISE.
 * IN NO EVENT SHALL WHISTLE COMMUNICATIONS BE LIABLE FOR ANY DAMAGES
 * RESULTING FROM OR ARISING OUT OF ANY USE OF THIS SOFTWARE, INCLUDING
 * WITHOUT LIMITATION, ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * PUNITIVE, OR CONSEQUENTIAL DAMAGES, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES, LOSS OF USE, DATA OR PROFITS, HOWEVER CAUSED AND UNDER ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF WHISTLE COMMUNICATIONS IS ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <err.h>
#include <netgraph.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "ngctl.h"

#define FMT		"  %-15s %-15s %-12s %-15s %-15s\n"
#define UNNAMED		"<unnamed>"
#define NOSTATUS	"<no status>"

static int ShowCmd(int ac, char **av);

const struct ngcmd show_cmd = {
	ShowCmd,
	"show [-n] <path>",
	"Show information about the node at <path>",
	"If the -n flag is given, hooks are not listed.",
	{ "inquire", "info" }
};

static int
ShowCmd(int ac, char **av)
{
	char *path;
	struct ng_mesg *resp;
	struct hooklist *hlist;
	struct nodeinfo *ninfo;
	int ch, no_hooks = 0;

	/* Get options */
	optind = 1;
	while ((ch = getopt(ac, av, "n")) != -1) {
		switch (ch) {
		case 'n':
			no_hooks = 1;
			break;
		case '?':
		default:
			return (CMDRTN_USAGE);
			break;
		}
	}
	ac -= optind;
	av += optind;

	/* Get arguments */
	switch (ac) {
	case 1:
		path = av[0];
		break;
	default:
		return (CMDRTN_USAGE);
	}

	/* Get node info and hook list */
	if (NgSendMsg(csock, path, NGM_GENERIC_COOKIE,
	    NGM_LISTHOOKS, NULL, 0) < 0) {
		warn("send msg");
		return (CMDRTN_ERROR);
	}
	if (NgAllocRecvMsg(csock, &resp, NULL) < 0) {
		warn("recv msg");
		return (CMDRTN_ERROR);
	}

	/* Show node information */
	hlist = (struct hooklist *) resp->data;
	ninfo = &hlist->nodeinfo;
	if (!*ninfo->name)
		snprintf(ninfo->name, sizeof(ninfo->name), "%s", UNNAMED);
	printf("  Name: %-15s Type: %-15s ID: %08x   Num hooks: %d\n",
	    ninfo->name, ninfo->type, ninfo->id, ninfo->hooks);
	if (!no_hooks && ninfo->hooks > 0) {
		u_int k;

		printf(FMT, "Local hook", "Peer name",
		    "Peer type", "Peer ID", "Peer hook");
		printf(FMT, "----------", "---------",
		    "---------", "-------", "---------");
		for (k = 0; k < ninfo->hooks; k++) {
			struct linkinfo *const link = &hlist->link[k];
			struct nodeinfo *const peer = &hlist->link[k].nodeinfo;
			char idbuf[20];

			if (!*peer->name) {
				snprintf(peer->name, sizeof(peer->name),
				  "%s", UNNAMED);
			}
			snprintf(idbuf, sizeof(idbuf), "%08x", peer->id);
			printf(FMT, link->ourhook, peer->name,
			    peer->type, idbuf, link->peerhook);
		}
	}
	free(resp);
	return (CMDRTN_OK);
}


