
/*
 * list.c
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

#define UNNAMED		"<unnamed>"

static int ListCmd(int ac, char **av);

const struct ngcmd list_cmd = {
	ListCmd,
	"list [-ln]",
	"Show information about all nodes",
	"The list command shows information about every node that currently"
	" exists in the netgraph system. The optional -n argument limits"
	" this list to only those nodes with a global name assignment."
	" The optional -l argument provides verbose output that includes"
	" hook information as well.",
	{ "ls" }
};

static int
ListCmd(int ac, char **av)
{
	struct ng_mesg *resp;
	struct namelist *nlist;
	struct nodeinfo *ninfo;
	int list_hooks = 0;
	int named_only = 0;
	int ch, rtn = CMDRTN_OK;

	/* Get options */
	optind = 1;
	while ((ch = getopt(ac, av, "ln")) != -1) {
		switch (ch) {
		case 'l':
			list_hooks = 1;
			break;
		case 'n':
			named_only = 1;
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
	case 0:
		break;
	default:
		return (CMDRTN_USAGE);
	}

	/* Get list of nodes */
	if (NgSendMsg(csock, ".", NGM_GENERIC_COOKIE,
	    named_only ? NGM_LISTNAMES : NGM_LISTNODES, NULL, 0) < 0) {
		warn("send msg");
		return (CMDRTN_ERROR);
	}
	if (NgAllocRecvMsg(csock, &resp, NULL) < 0) {
		warn("recv msg");
		return (CMDRTN_ERROR);
	}

	/* Show each node */
	nlist = (struct namelist *) resp->data;
	printf("There are %d total %snodes:\n",
	    nlist->numnames, named_only ? "named " : "");
	ninfo = nlist->nodeinfo;
	if (list_hooks) {
		char	path[NG_PATHSIZ];
		char	*argv[2] = { "show", path };

		while (nlist->numnames > 0) {
			snprintf(path, sizeof(path),
			    "[%lx]:", (u_long)ninfo->id);
			if ((rtn = (*show_cmd.func)(2, argv)) != CMDRTN_OK)
				break;
			ninfo++;
			nlist->numnames--;
			if (nlist->numnames > 0)
				printf("\n");
		}
	} else {
		while (nlist->numnames > 0) {
			if (!*ninfo->name)
				snprintf(ninfo->name, sizeof(ninfo->name),
				    "%s", UNNAMED);
			printf("  Name: %-15s Type: %-15s ID: %08x   "
			    "Num hooks: %d\n",
			    ninfo->name, ninfo->type, ninfo->id, ninfo->hooks);
			ninfo++;
			nlist->numnames--;
		}
	}

	/* Done */
	free(resp);
	return (rtn);
}

