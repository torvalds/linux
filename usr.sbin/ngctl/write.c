
/*
 * write.c
 *
 * Copyright (c) 2002 Archie L. Cobbs
 * All rights reserved.
 * 
 * Subject to the following obligations and disclaimer of warranty, use and
 * redistribution of this software, in source or object code forms, with or
 * without modifications are expressly permitted by Archie L. Cobbs;
 * provided, however, that:
 * 1. Any and all reproductions of the source or object code must include the
 *    copyright notice above and the following disclaimer of warranties
 * 
 * THIS SOFTWARE IS BEING PROVIDED BY ARCHIE L. COBBS AS IS", AND TO
 * THE MAXIMUM EXTENT PERMITTED BY LAW, ARCHIE L. COBBS MAKES NO
 * REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED, REGARDING THIS SOFTWARE,
 * INCLUDING WITHOUT LIMITATION, ANY AND ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT.
 * ARCHIE L. COBBS DOES NOT WARRANT, GUARANTEE, OR MAKE ANY
 * REPRESENTATIONS REGARDING THE USE OF, OR THE RESULTS OF THE USE OF THIS
 * SOFTWARE IN TERMS OF ITS CORRECTNESS, ACCURACY, RELIABILITY OR OTHERWISE.
 * IN NO EVENT SHALL ARCHIE L. COBBS BE LIABLE FOR ANY DAMAGES
 * RESULTING FROM OR ARISING OUT OF ANY USE OF THIS SOFTWARE, INCLUDING
 * WITHOUT LIMITATION, ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * PUNITIVE, OR CONSEQUENTIAL DAMAGES, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES, LOSS OF USE, DATA OR PROFITS, HOWEVER CAUSED AND UNDER ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ARCHIE L. COBBS IS ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <err.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <netgraph/ng_socket.h>

#include "ngctl.h"

#define BUF_SIZE	8192

static int WriteCmd(int ac, char **av);

const struct ngcmd write_cmd = {
	WriteCmd,
	"write hook < -f file | byte ... >",
	"Send a data packet down the hook named by \"hook\".",
	"The data may be contained in a file, or may be described directly"
	" on the command line by supplying a sequence of bytes.",
	{ "w" }
};

static int
WriteCmd(int ac, char **av)
{
	u_int32_t sagbuf[64];
	struct sockaddr_ng *sag = (struct sockaddr_ng *)sagbuf;
	u_char buf[BUF_SIZE];
	const char *hook;
	FILE *fp;
	u_int len;
	int byte;
	int i;

	/* Get arguments */
	if (ac < 3)
		return (CMDRTN_USAGE);
	hook = av[1];

	/* Get data */
	if (strcmp(av[2], "-f") == 0) {
		if (ac != 4)
			return (CMDRTN_USAGE);
		if ((fp = fopen(av[3], "r")) == NULL) {
			warn("can't read file \"%s\"", av[3]);
			return (CMDRTN_ERROR);
		}
		if ((len = fread(buf, 1, sizeof(buf), fp)) == 0) {
			if (ferror(fp))
				warn("can't read file \"%s\"", av[3]);
			else
				warnx("file \"%s\" is empty", av[3]);
			fclose(fp);
			return (CMDRTN_ERROR);
		}
		fclose(fp);
	} else {
		for (i = 2, len = 0; i < ac && len < sizeof(buf); i++, len++) {
			if (sscanf(av[i], "%i", &byte) != 1
			    || (byte < -128 || byte > 255)) {
				warnx("invalid byte \"%s\"", av[i]);
				return (CMDRTN_ERROR);
			}
			buf[len] = (u_char)byte;
		}
		if (len == 0)
			return (CMDRTN_USAGE);
	}

	/* Send data */
	sag->sg_len = 3 + strlen(hook);
	sag->sg_family = AF_NETGRAPH;
	strlcpy(sag->sg_data, hook, sizeof(sagbuf) - 2);
	if (sendto(dsock, buf, len,
	    0, (struct sockaddr *)sag, sag->sg_len) == -1) {
		warn("writing to hook \"%s\"", hook);
		return (CMDRTN_ERROR);
	}

	/* Done */
	return (CMDRTN_OK);
}

