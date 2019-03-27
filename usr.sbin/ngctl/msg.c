
/*
 * msg.c
 *
 * Copyright (c) 1999 Whistle Communications, Inc.
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
 * $Whistle: msg.c,v 1.2 1999/11/29 23:38:35 archie Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <err.h>
#include <netgraph.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "ngctl.h"

static int MsgCmd(int ac, char **av);

const struct ngcmd msg_cmd = {
	MsgCmd,
	"msg path command [args ... ]",
	"Send a netgraph control message to the node at \"path\"",
	"The msg command constructs a netgraph control message from the"
	" command name and ASCII arguments (if any) and sends that message"
	" to the node.  It does this by first asking the node to convert"
	" the ASCII message into binary format, and resending the result.",
	{ "cmd" }
};

static int
MsgCmd(int ac, char **av)
{
	char *buf;
	char *path, *cmdstr;
	int i, len;

	/* Get arguments */
	if (ac < 3)
		return (CMDRTN_USAGE);
	path = av[1];
	cmdstr = av[2];

	/* Put command and arguments back together as one string */
	for (len = 1, i = 3; i < ac; i++)
		len += strlen(av[i]) + 1;
	if ((buf = malloc(len)) == NULL) {
		warn("malloc");
		return (CMDRTN_ERROR);
	}
	for (*buf = '\0', i = 3; i < ac; i++) {
		snprintf(buf + strlen(buf),
		    len - strlen(buf), " %s", av[i]);
	}

	/* Send it */
	if (NgSendAsciiMsg(csock, path, "%s%s", cmdstr, buf) < 0) {
		free(buf);
		warn("send msg");
		return (CMDRTN_ERROR);
	}
	free(buf);

	/* See if a synchronous reply awaits */
	{
		struct timeval tv;
		fd_set rfds;

		FD_ZERO(&rfds);
		FD_SET(csock, &rfds);
		memset(&tv, 0, sizeof(tv));
		switch (select(csock + 1, &rfds, NULL, NULL, &tv)) {
		case -1:
			err(EX_OSERR, "select");
		case 0:
			break;
		default:
			MsgRead();
			break;
		}
	}

	/* Done */
	return (CMDRTN_OK);
}

/*
 * Read and display the next incoming control message
 */
void
MsgRead(void)
{
	struct ng_mesg *m, *m2;
	struct ng_mesg *ascii;
	char path[NG_PATHSIZ];

	/* Get incoming message (in binary form) */
	if (NgAllocRecvMsg(csock, &m, path) < 0) {
		warn("recv incoming message");
		return;
	}

	/* Ask originating node to convert message to ASCII */
	if (NgSendMsg(csock, path, NGM_GENERIC_COOKIE,
	      NGM_BINARY2ASCII, m, sizeof(*m) + m->header.arglen) < 0
	    || NgAllocRecvMsg(csock, &m2, NULL) < 0) {
		printf("Rec'd %s %d from \"%s\":\n",
		    (m->header.flags & NGF_RESP) != 0 ? "response" : "command",
		    m->header.cmd, path);
		if (m->header.arglen == 0)
			printf("No arguments\n");
		else
			DumpAscii((const u_char *)m->data, m->header.arglen);
		free(m);
		return;
	}

	/* Display message in ASCII form */
	free(m);
	ascii = (struct ng_mesg *)m2->data;
	printf("Rec'd %s \"%s\" (%d) from \"%s\":\n",
	    (ascii->header.flags & NGF_RESP) != 0 ? "response" : "command",
	    ascii->header.cmdstr, ascii->header.cmd, path);
	if (*ascii->data != '\0')
		printf("Args:\t%s\n", ascii->data);
	else
		printf("No arguments\n");
	free(m2);
}

