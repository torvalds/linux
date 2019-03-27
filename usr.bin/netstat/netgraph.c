/*-
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/socket.h>
#define	_WANT_SOCKET
#include <sys/socketvar.h>
#include <sys/protosw.h>
#include <sys/linker.h>

#include <net/route.h>

#include <netgraph.h>
#include <netgraph/ng_message.h>
#include <netgraph/ng_socket.h>
#include <netgraph/ng_socketvar.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <libxo/xo.h>
#include "netstat.h"

static	int first = 1;
static	int csock = -1;

void
netgraphprotopr(u_long off, const char *name, int af1 __unused,
    int proto __unused)
{
	struct ngpcb *this, *next;
	struct ngpcb ngpcb;
	struct socket sockb;
	int debug = 1;

	/* If symbol not found, try looking in the KLD module */
	if (off == 0) {
		if (debug)
			xo_warnx("Error reading symbols from ng_socket.ko");
		return;
	}

	/* Get pointer to first socket */
	kread(off, (char *)&this, sizeof(this));

	/* Get my own socket node */
	if (csock == -1)
		NgMkSockNode(NULL, &csock, NULL);

	for (; this != NULL; this = next) {
		u_char rbuf[sizeof(struct ng_mesg) + sizeof(struct nodeinfo)];
		struct ng_mesg *resp = (struct ng_mesg *) rbuf;
		struct nodeinfo *ni = (struct nodeinfo *) resp->data;
		char path[64];

		/* Read in ngpcb structure */
		kread((u_long)this, (char *)&ngpcb, sizeof(ngpcb));
		next = LIST_NEXT(&ngpcb, socks);

		/* Read in socket structure */
		kread((u_long)ngpcb.ng_socket, (char *)&sockb, sizeof(sockb));

		/* Check type of socket */
		if (strcmp(name, "ctrl") == 0 && ngpcb.type != NG_CONTROL)
			continue;
		if (strcmp(name, "data") == 0 && ngpcb.type != NG_DATA)
			continue;

		/* Do headline */
		if (first) {
			xo_emit("{T:Netgraph sockets}\n");
			if (Aflag)
				xo_emit("{T:/%-8.8s} ", "PCB");
			xo_emit("{T:/%-5.5s} {T:/%-6.6s} {T:/%-6.6s} "
			    "{T:/%-14.14s} {T:/%s}\n",
			    "Type", "Recv-Q", "Send-Q", "Node Address",
			    "#Hooks");
			first = 0;
		}

		/* Show socket */
		if (Aflag)
			xo_emit("{:address/%8lx} ", (u_long) this);
		xo_emit("{t:name/%-5.5s} {:receive-bytes-waiting/%6u} "
		    "{:send-byte-waiting/%6u} ",
		    name, sockb.so_rcv.sb_ccc, sockb.so_snd.sb_ccc);

		/* Get info on associated node */
		if (ngpcb.node_id == 0 || csock == -1)
			goto finish;
		snprintf(path, sizeof(path), "[%x]:", ngpcb.node_id);
		if (NgSendMsg(csock, path,
		    NGM_GENERIC_COOKIE, NGM_NODEINFO, NULL, 0) < 0)
			goto finish;
		if (NgRecvMsg(csock, resp, sizeof(rbuf), NULL) < 0)
			goto finish;

		/* Display associated node info */
		if (*ni->name != '\0')
			snprintf(path, sizeof(path), "%s:", ni->name);
		xo_emit("{t:path/%-14.14s} {:hooks/%4d}", path, ni->hooks);
finish:
		xo_emit("\n");
	}
}

