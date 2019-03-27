/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2002 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by Network
 * Associates Laboratories, the Security Research Division of Network
 * Associates, Inc. under DARPA/SPAWAR contract N66001-01-C-8035
 * ("CBOSS"), as part of the DARPA CHATS research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the authors may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
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
#include <sys/types.h>
#include <sys/mac.h>

#include <err.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#define	MAXELEMENTS	32

static void
usage(void)
{

	fprintf(stderr, "getpmac [-l list,of,labels] [-p pid]\n");
	exit (EX_USAGE);
}

int
main(int argc, char *argv[])
{
	char *labellist, *string;
	mac_t label;
	pid_t pid;
	int ch, error, pid_set;

	pid_set = 0;
	pid = 0;
	labellist = NULL;
	while ((ch = getopt(argc, argv, "l:p:")) != -1) {
		switch (ch) {
		case 'l':
			if (labellist != NULL)
				usage();
			labellist = argv[optind - 1];
			break;
		case 'p':
			if (pid_set)
				usage();
			pid = atoi(argv[optind - 1]);
			pid_set = 1;
			break;
		default:
			usage();
		}

	}

	argc -= optind;
	argv += optind;

	if (argc != 0)
		usage();

	if (labellist != NULL)
		error = mac_prepare(&label, labellist);
	else
		error = mac_prepare_process_label(&label);
	if (error != 0) {
		perror("mac_prepare");
		return (-1);
	}

	if (pid_set) {
		error = mac_get_pid(pid, label);
		if (error)
			perror("mac_get_pid");
	} else {
		error = mac_get_proc(label);
		if (error)
			perror("mac_get_proc");
	}
	if (error) {
		mac_free(label);
		exit (-1);
	}
	error = mac_to_text(label, &string);
	if (error != 0) {
		perror("mac_to_text");
		exit(EX_DATAERR);
	}

	if (strlen(string) > 0)
		printf("%s\n", string);
		
	mac_free(label);
	free(string);
	exit(EX_OK);
}
