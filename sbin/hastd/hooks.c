/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 The FreeBSD Foundation
 * Copyright (c) 2010 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * All rights reserved.
 *
 * This software was developed by Pawel Jakub Dawidek under sponsorship from
 * the FreeBSD Foundation.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <pjdlog.h>

#include "hooks.h"
#include "subr.h"
#include "synch.h"

/* Report processes that are running for too long not often than this value. */
#define	REPORT_INTERVAL	60

/* Are we initialized? */
static bool hooks_initialized = false;

/*
 * Keep all processes we forked on a global queue, so we can report nicely
 * when they finish or report that they are running for a long time.
 */
#define	HOOKPROC_MAGIC_ALLOCATED	0x80090ca
#define	HOOKPROC_MAGIC_ONLIST		0x80090c0
struct hookproc {
	/* Magic. */
	int	hp_magic;
	/* PID of a forked child. */
	pid_t	hp_pid;
	/* When process were forked? */
	time_t	hp_birthtime;
	/* When we logged previous reported? */
	time_t	hp_lastreport;
	/* Path to executable and all the arguments we passed. */
	char	hp_comm[PATH_MAX];
	TAILQ_ENTRY(hookproc) hp_next;
};
static TAILQ_HEAD(, hookproc) hookprocs;
static pthread_mutex_t hookprocs_lock;

static void hook_remove(struct hookproc *hp);
static void hook_free(struct hookproc *hp);

static void
descriptors(void)
{
	int fd;

	/*
	 * Close all (or almost all) descriptors.
	 */
	if (pjdlog_mode_get() == PJDLOG_MODE_STD) {
		closefrom(MAX(MAX(STDIN_FILENO, STDOUT_FILENO),
		    STDERR_FILENO) + 1);
		return;
	}

	closefrom(0);

	/*
	 * Redirect stdin, stdout and stderr to /dev/null.
	 */
	fd = open(_PATH_DEVNULL, O_RDONLY);
	if (fd == -1) {
		pjdlog_errno(LOG_WARNING, "Unable to open %s for reading",
		    _PATH_DEVNULL);
	} else if (fd != STDIN_FILENO) {
		if (dup2(fd, STDIN_FILENO) == -1) {
			pjdlog_errno(LOG_WARNING,
			    "Unable to duplicate descriptor for stdin");
		}
		close(fd);
	}
	fd = open(_PATH_DEVNULL, O_WRONLY);
	if (fd == -1) {
		pjdlog_errno(LOG_WARNING, "Unable to open %s for writing",
		    _PATH_DEVNULL);
	} else {
		if (fd != STDOUT_FILENO && dup2(fd, STDOUT_FILENO) == -1) {
			pjdlog_errno(LOG_WARNING,
			    "Unable to duplicate descriptor for stdout");
		}
		if (fd != STDERR_FILENO && dup2(fd, STDERR_FILENO) == -1) {
			pjdlog_errno(LOG_WARNING,
			    "Unable to duplicate descriptor for stderr");
		}
		if (fd != STDOUT_FILENO && fd != STDERR_FILENO)
			close(fd);
	}
}

void
hook_init(void)
{

	PJDLOG_ASSERT(!hooks_initialized);

	mtx_init(&hookprocs_lock);
	TAILQ_INIT(&hookprocs);
	hooks_initialized = true;
}

void
hook_fini(void)
{
	struct hookproc *hp;

	PJDLOG_ASSERT(hooks_initialized);

	mtx_lock(&hookprocs_lock);
	while ((hp = TAILQ_FIRST(&hookprocs)) != NULL) {
		PJDLOG_ASSERT(hp->hp_magic == HOOKPROC_MAGIC_ONLIST);
		PJDLOG_ASSERT(hp->hp_pid > 0);

		hook_remove(hp);
		hook_free(hp);
	}
	mtx_unlock(&hookprocs_lock);

	mtx_destroy(&hookprocs_lock);
	TAILQ_INIT(&hookprocs);
	hooks_initialized = false;
}

static struct hookproc *
hook_alloc(const char *path, char **args)
{
	struct hookproc *hp;
	unsigned int ii;

	hp = malloc(sizeof(*hp));
	if (hp == NULL) {
		pjdlog_error("Unable to allocate %zu bytes of memory for a hook.",
		    sizeof(*hp));
		return (NULL);
	}

	hp->hp_pid = 0;
	hp->hp_birthtime = hp->hp_lastreport = time(NULL);
	(void)strlcpy(hp->hp_comm, path, sizeof(hp->hp_comm));
	/* We start at 2nd argument as we don't want to have exec name twice. */
	for (ii = 1; args[ii] != NULL; ii++) {
		(void)snprlcat(hp->hp_comm, sizeof(hp->hp_comm), " %s",
		    args[ii]);
	}
	if (strlen(hp->hp_comm) >= sizeof(hp->hp_comm) - 1) {
		pjdlog_error("Exec path too long, correct configuration file.");
		free(hp);
		return (NULL);
	}
	hp->hp_magic = HOOKPROC_MAGIC_ALLOCATED;
	return (hp);
}

static void
hook_add(struct hookproc *hp, pid_t pid)
{

	PJDLOG_ASSERT(hp->hp_magic == HOOKPROC_MAGIC_ALLOCATED);
	PJDLOG_ASSERT(hp->hp_pid == 0);

	hp->hp_pid = pid;
	mtx_lock(&hookprocs_lock);
	hp->hp_magic = HOOKPROC_MAGIC_ONLIST;
	TAILQ_INSERT_TAIL(&hookprocs, hp, hp_next);
	mtx_unlock(&hookprocs_lock);
}

static void
hook_remove(struct hookproc *hp)
{

	PJDLOG_ASSERT(hp->hp_magic == HOOKPROC_MAGIC_ONLIST);
	PJDLOG_ASSERT(hp->hp_pid > 0);
	PJDLOG_ASSERT(mtx_owned(&hookprocs_lock));

	TAILQ_REMOVE(&hookprocs, hp, hp_next);
	hp->hp_magic = HOOKPROC_MAGIC_ALLOCATED;
}

static void
hook_free(struct hookproc *hp)
{

	PJDLOG_ASSERT(hp->hp_magic == HOOKPROC_MAGIC_ALLOCATED);
	PJDLOG_ASSERT(hp->hp_pid > 0);

	hp->hp_magic = 0;
	free(hp);
}

static struct hookproc *
hook_find(pid_t pid)
{
	struct hookproc *hp;

	PJDLOG_ASSERT(mtx_owned(&hookprocs_lock));

	TAILQ_FOREACH(hp, &hookprocs, hp_next) {
		PJDLOG_ASSERT(hp->hp_magic == HOOKPROC_MAGIC_ONLIST);
		PJDLOG_ASSERT(hp->hp_pid > 0);

		if (hp->hp_pid == pid)
			break;
	}

	return (hp);
}

void
hook_check_one(pid_t pid, int status)
{
	struct hookproc *hp;

	mtx_lock(&hookprocs_lock);
	hp = hook_find(pid);
	if (hp == NULL) {
		mtx_unlock(&hookprocs_lock);
		pjdlog_debug(1, "Unknown process pid=%u", pid);
		return;
	}
	hook_remove(hp);
	mtx_unlock(&hookprocs_lock);
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
		pjdlog_debug(1, "Hook exited gracefully (pid=%u, cmd=[%s]).",
		    pid, hp->hp_comm);
	} else if (WIFSIGNALED(status)) {
		pjdlog_error("Hook was killed (pid=%u, signal=%d, cmd=[%s]).",
		    pid, WTERMSIG(status), hp->hp_comm);
	} else {
		pjdlog_error("Hook exited ungracefully (pid=%u, exitcode=%d, cmd=[%s]).",
		    pid, WIFEXITED(status) ? WEXITSTATUS(status) : -1,
		    hp->hp_comm);
	}
	hook_free(hp);
}

void
hook_check(void)
{
	struct hookproc *hp, *hp2;
	time_t now;

	PJDLOG_ASSERT(hooks_initialized);

	pjdlog_debug(2, "Checking hooks.");

	/*
	 * Report about processes that are running for a long time.
	 */
	now = time(NULL);
	mtx_lock(&hookprocs_lock);
	TAILQ_FOREACH_SAFE(hp, &hookprocs, hp_next, hp2) {
		PJDLOG_ASSERT(hp->hp_magic == HOOKPROC_MAGIC_ONLIST);
		PJDLOG_ASSERT(hp->hp_pid > 0);

		/*
		 * If process doesn't exists we somehow missed it.
		 * Not much can be done expect for logging this situation.
		 */
		if (kill(hp->hp_pid, 0) == -1 && errno == ESRCH) {
			pjdlog_warning("Hook disappeared (pid=%u, cmd=[%s]).",
			    hp->hp_pid, hp->hp_comm);
			hook_remove(hp);
			hook_free(hp);
			continue;
		}

		/*
		 * Skip proccesses younger than 1 minute.
		 */
		if (now - hp->hp_lastreport < REPORT_INTERVAL)
			continue;

		/*
		 * Hook is running for too long, report it.
		 */
		pjdlog_warning("Hook is running for %ju seconds (pid=%u, cmd=[%s]).",
		    (uintmax_t)(now - hp->hp_birthtime), hp->hp_pid,
		    hp->hp_comm);
		hp->hp_lastreport = now;
	}
	mtx_unlock(&hookprocs_lock);
}

void
hook_exec(const char *path, ...)
{
	va_list ap;

	va_start(ap, path);
	hook_execv(path, ap);
	va_end(ap);
}

void
hook_execv(const char *path, va_list ap)
{
	struct hookproc *hp;
	char *args[64];
	unsigned int ii;
	sigset_t mask;
	pid_t pid;

	PJDLOG_ASSERT(hooks_initialized);

	if (path == NULL || path[0] == '\0')
		return;

	memset(args, 0, sizeof(args));
	args[0] = __DECONST(char *, path);
	for (ii = 1; ii < sizeof(args) / sizeof(args[0]); ii++) {
		args[ii] = va_arg(ap, char *);
		if (args[ii] == NULL)
			break;
	}
	PJDLOG_ASSERT(ii < sizeof(args) / sizeof(args[0]));

	hp = hook_alloc(path, args);
	if (hp == NULL)
		return;

	pjdlog_debug(1, "Executing hook: %s", hp->hp_comm);

	pid = fork();
	switch (pid) {
	case -1:	/* Error. */
		pjdlog_errno(LOG_ERR, "Unable to fork to execute %s", path);
		hook_free(hp);
		return;
	case 0:		/* Child. */
		descriptors();
		PJDLOG_VERIFY(sigemptyset(&mask) == 0);
		PJDLOG_VERIFY(sigprocmask(SIG_SETMASK, &mask, NULL) == 0);
		/*
		 * Dummy handler set for SIGCHLD in the parent will be restored
		 * to SIG_IGN on execv(3) below, so there is no need to do
		 * anything with it.
		 */
		execv(path, args);
		pjdlog_errno(LOG_ERR, "Unable to execute %s", path);
		exit(EX_SOFTWARE);
	default:	/* Parent. */
		hook_add(hp, pid);
		break;
	}
}
