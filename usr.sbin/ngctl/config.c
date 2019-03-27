/*
 * config.c
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
#include <errno.h>
#include <netgraph.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "ngctl.h"

#define NOCONFIG	"<no config>"

static int ConfigCmd(int ac, char **av);

const struct ngcmd config_cmd = {
	ConfigCmd,
	"config <path> [arguments]",
	"get or set configuration of node at <path>",
	NULL,
	{ NULL }
};

static int
ConfigCmd(int ac, char **av)
{
	u_char sbuf[sizeof(struct ng_mesg) + NG_TEXTRESPONSE];
	struct ng_mesg *const resp = (struct ng_mesg *) sbuf;
	char *const status = (char *) resp->data;
	char *path;
	char buf[NG_TEXTRESPONSE];
	int nostat = 0, i;

	/* Get arguments */
	if (ac < 2)
		return (CMDRTN_USAGE);
	path = av[1];

	*buf = '\0';
	for (i = 2; i < ac; i++) {
		if (i != 2)
			strcat(buf, " ");
		strcat(buf, av[i]);
	}
	
	/* Get node config summary */
	if (*buf != '\0')
		i = NgSendMsg(csock, path, NGM_GENERIC_COOKIE,
	            NGM_TEXT_CONFIG, buf, strlen(buf) + 1);
	else
		i = NgSendMsg(csock, path, NGM_GENERIC_COOKIE,
	            NGM_TEXT_CONFIG, NULL, 0);
	if (i < 0) {
		switch (errno) {
		case EINVAL:
			nostat = 1;
			break;
		default:
			warn("send msg");
			return (CMDRTN_ERROR);
		}
	} else {
		if (NgRecvMsg(csock, resp, sizeof(sbuf), NULL) < 0
		    || (resp->header.flags & NGF_RESP) == 0)
			nostat = 1;
	}

	/* Show it */
	if (nostat)
		printf("No config available for \"%s\"\n", path);
	else
		printf("Config for \"%s\":\n%s\n", path, status);
	return (CMDRTN_OK);
}

