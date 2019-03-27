/*-
 * Copyright (c) 2013-2018 Devin Teske <dteske@FreeBSD.org>
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <err.h>
#include <limits.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "util.h"

extern char **environ;
static char cmdbuf[CMDBUFMAX]	= "";
static char shellcmd[PATH_MAX]	= PATH_SHELL;
static char *shellcmd_argv[6]	= {
	shellcmd,
	__DECONST(char *, "-c"),
	cmdbuf,
	__DECONST(char *, "--"),
	shellcmd,
	NULL,
};

/*
 * Spawn a sh(1) command. Writes the resulting process ID to the pid_t pointed
 * at by `pid'. Returns a file descriptor (int) suitable for writing data to
 * the spawned command (data written to file descriptor is seen as standard-in
 * by the spawned sh(1) command). Returns `-1' if unable to spawn command.
 *
 * If cmd contains a single "%s" sequence, replace it with label if non-NULL.
 */
int
shell_spawn_pipecmd(const char *cmd, const char *label, pid_t *pid)
{
	int error;
	int len;
	posix_spawn_file_actions_t action;
#if SHELL_SPAWN_DEBUG
	unsigned int i;
#endif
	int stdin_pipe[2] = { -1, -1 };

	/* Populate argument array */
	if (label != NULL && fmtcheck(cmd, "%s") == cmd)
		len = snprintf(cmdbuf, CMDBUFMAX, cmd, label);
	else
		len = snprintf(cmdbuf, CMDBUFMAX, "%s", cmd);
	if (len >= CMDBUFMAX) {
		warnx("%s:%d:%s: cmdbuf[%u] too small to hold cmd argument",
		    __FILE__, __LINE__, __func__, CMDBUFMAX);
		return (-1);
	}

	/* Open a pipe to communicate with [X]dialog(1) */
	if (pipe(stdin_pipe) < 0)
		err(EXIT_FAILURE, "%s: pipe(2)", __func__);

	/* Fork sh(1) process */
#if SHELL_SPAWN_DEBUG
	fprintf(stderr, "%s: spawning `", __func__);
	for (i = 0; shellcmd_argv[i] != NULL; i++) {
		if (i == 0)
			fprintf(stderr, "%s", shellcmd_argv[i]);
		else if (i == 2)
			fprintf(stderr, " '%s'", shellcmd_argv[i]);
		else
			fprintf(stderr, " %s", shellcmd_argv[i]);
	}
	fprintf(stderr, "'\n");
#endif
	posix_spawn_file_actions_init(&action);
	posix_spawn_file_actions_adddup2(&action, stdin_pipe[0], STDIN_FILENO);
	posix_spawn_file_actions_addclose(&action, stdin_pipe[1]);
	error = posix_spawnp(pid, shellcmd, &action,
	    (const posix_spawnattr_t *)NULL, shellcmd_argv, environ);
	if (error != 0) err(EXIT_FAILURE, "%s", shellcmd);

	return stdin_pipe[1];
}
