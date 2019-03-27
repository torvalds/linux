/*-
 * sdpcontrol.c
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001-2003 Maksim Yevmenkin <m_evmenkin@yahoo.com>
 * All rights reserved.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: sdpcontrol.c,v 1.1 2003/09/08 02:27:27 max Exp $
 * $FreeBSD$
 */

#include <assert.h>
#define L2CAP_SOCKET_CHECKED
#include <bluetooth.h>
#include <err.h>
#include <errno.h>
#include <sdp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "sdpcontrol.h"

/* Prototypes */
static int                  do_sdp_command	(bdaddr_p, char const *, int,
						 int, char **);
static struct sdp_command * find_sdp_command	(char const *,
						 struct sdp_command *); 
static void                 print_sdp_command	(struct sdp_command *);
static void                 usage		(void);

/* Main */
int
main(int argc, char *argv[])
{
	char const	*control = SDP_LOCAL_PATH;
	int		 n, local;
	bdaddr_t	 bdaddr;

	memset(&bdaddr, 0, sizeof(bdaddr));
	local = 0;

	/* Process command line arguments */
	while ((n = getopt(argc, argv, "a:c:lh")) != -1) {
		switch (n) {
		case 'a': /* bdaddr */
			if (!bt_aton(optarg, &bdaddr)) {
				struct hostent  *he = NULL;

				if ((he = bt_gethostbyname(optarg)) == NULL)
					errx(1, "%s: %s", optarg, hstrerror(h_errno));
 
				memcpy(&bdaddr, he->h_addr, sizeof(bdaddr));
			}
			break;

		case 'c': /* control socket */
			control = optarg;
			break;

		case 'l': /* local sdpd */
			local = 1;
			break;

		case 'h':
		default:
			usage();
			/* NOT REACHED */
		}
	}

	argc -= optind; 
	argv += optind;

	if (*argv == NULL)
		usage();

	return (do_sdp_command(&bdaddr, control, local, argc, argv));
}

/* Execute commands */
static int
do_sdp_command(bdaddr_p bdaddr, char const *control, int local,
		int argc, char **argv)
{
	char			*cmd = argv[0];
	struct sdp_command	*c = NULL;
	void			*xs = NULL;
	int			 e, help;

	help = 0;
	if (strcasecmp(cmd, "help") == 0) {
		argc --;
		argv ++;

		if (argc <= 0) {
			fprintf(stdout, "Supported commands:\n");
			print_sdp_command(sdp_commands);
			fprintf(stdout, "\nFor more information use " \
				"'help command'\n");

			return (OK);
		}

		help = 1;
		cmd = argv[0];
	}

	c = find_sdp_command(cmd, sdp_commands);
	if (c == NULL) {
		fprintf(stdout, "Unknown command: \"%s\"\n", cmd);
		return (ERROR);
	}

	if (!help) {
		if (!local) {
			if (memcmp(bdaddr, NG_HCI_BDADDR_ANY, sizeof(*bdaddr)) == 0)
				usage();

			xs = sdp_open(NG_HCI_BDADDR_ANY, bdaddr);
		} else
			xs = sdp_open_local(control);

		if (xs == NULL)
			errx(1, "Could not create SDP session object");
		if (sdp_error(xs) == 0)
			e = (c->handler)(xs, -- argc, ++ argv);
		else
			e = ERROR;
	} else
		e = USAGE;

	switch (e) {
	case OK:
	case FAILED:
		break;

	case ERROR:
		fprintf(stdout, "Could not execute command \"%s\". %s\n",
			cmd, strerror(sdp_error(xs)));
		break;

	case USAGE:
		fprintf(stdout, "Usage: %s\n%s\n", c->command, c->description);
		break;

	default: assert(0); break;
	}

	sdp_close(xs);

	return (e);
} /* do_sdp_command */

/* Try to find command in specified category */
static struct sdp_command *
find_sdp_command(char const *command, struct sdp_command *category)   
{
	struct sdp_command	*c = NULL;

	for (c = category; c->command != NULL; c++) {
		char	*c_end = strchr(c->command, ' ');

		if (c_end != NULL) {
			int	len = c_end - c->command;

			if (strncasecmp(command, c->command, len) == 0)
				return (c);
		} else if (strcasecmp(command, c->command) == 0)
				return (c);
	}

	return (NULL);
} /* find_sdp_command */

/* Print commands in specified category */
static void
print_sdp_command(struct sdp_command *category)
{
	struct sdp_command	*c = NULL;

	for (c = category; c->command != NULL; c++)
		fprintf(stdout, "\t%s\n", c->command);
} /* print_sdp_command */

/* Usage */
static void
usage(void)
{
	fprintf(stderr,
"Usage: sdpcontrol options command\n" \
"Where options are:\n"
"	-a address	address to connect to\n" \
"	-c path		path to the control socket (default is %s)\n" \
"	-h		display usage and quit\n" \
"	-l		connect to the local SDP server via control socket\n" \
"	command		one of the supported commands\n", SDP_LOCAL_PATH);
	exit(255);
} /* usage */

