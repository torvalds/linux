
/*
 * types.c
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

static int TypesCmd(int ac, char **av);

const struct ngcmd types_cmd = {
	TypesCmd,
	"types",
	"Show information about all installed node types",
	NULL,
	{ NULL }
};

static int
TypesCmd(int ac, char **av __unused)
{
	struct ng_mesg *resp;
	struct typelist *tlist;
	int rtn = CMDRTN_OK;
	u_int k;

	/* Get arguments */
	switch (ac) {
	case 1:
		break;
	default:
		return (CMDRTN_USAGE);
	}

	/* Get list of types */
	if (NgSendMsg(csock, ".", NGM_GENERIC_COOKIE,
	    NGM_LISTTYPES, NULL, 0) < 0) {
		warn("send msg");
		return (CMDRTN_ERROR);
	}
	if (NgAllocRecvMsg(csock, &resp, NULL) < 0) {
		warn("recv msg");
		return (CMDRTN_ERROR);
	}

	/* Show each type */
	tlist = (struct typelist *) resp->data;
	printf("There are %d total types:\n", tlist->numtypes);
	if (tlist->numtypes > 0) {
		printf("%15s   Number of living nodes\n", "Type name");
		printf("%15s   ----------------------\n", "---------");
	}
	for (k = 0; k < tlist->numtypes; k++) {
		struct typeinfo *const ti = &tlist->typeinfo[k];
		printf("%15s   %5d\n", ti->type_name, ti->numnodes);
	}

	/* Done */
	free(resp);
	return (rtn);
}

