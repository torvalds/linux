/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2001,2003 Networks Associates Technology, Inc.
 * Copyright (c) 2017 Dag-Erling Smørgrav
 * Copyright (c) 2018 Thomas Munro
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by ThinkSec AS and
 * NAI Labs, the Security Research Division of Network Associates, Inc.
 * under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"), as part of the
 * DARPA CHATS research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/poll.h>
#include <sys/procdesc.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <security/pam_appl.h>
#include <security/pam_modules.h>
#include <security/openpam.h>

#define PAM_ITEM_ENV(n) { (n), #n }
static struct {
	int item;
	const char *name;
} pam_item_env[] = {
	PAM_ITEM_ENV(PAM_SERVICE),
	PAM_ITEM_ENV(PAM_USER),
	PAM_ITEM_ENV(PAM_TTY),
	PAM_ITEM_ENV(PAM_RHOST),
	PAM_ITEM_ENV(PAM_RUSER),
};
#define NUM_PAM_ITEM_ENV (sizeof(pam_item_env) / sizeof(pam_item_env[0]))

#define PAM_ERR_ENV_X(str, num) str "=" #num
#define PAM_ERR_ENV(pam_err) PAM_ERR_ENV_X(#pam_err, pam_err)
static const char *pam_err_env[] = {
	PAM_ERR_ENV(PAM_SUCCESS),
	PAM_ERR_ENV(PAM_OPEN_ERR),
	PAM_ERR_ENV(PAM_SYMBOL_ERR),
	PAM_ERR_ENV(PAM_SERVICE_ERR),
	PAM_ERR_ENV(PAM_SYSTEM_ERR),
	PAM_ERR_ENV(PAM_BUF_ERR),
	PAM_ERR_ENV(PAM_CONV_ERR),
	PAM_ERR_ENV(PAM_PERM_DENIED),
	PAM_ERR_ENV(PAM_MAXTRIES),
	PAM_ERR_ENV(PAM_AUTH_ERR),
	PAM_ERR_ENV(PAM_NEW_AUTHTOK_REQD),
	PAM_ERR_ENV(PAM_CRED_INSUFFICIENT),
	PAM_ERR_ENV(PAM_AUTHINFO_UNAVAIL),
	PAM_ERR_ENV(PAM_USER_UNKNOWN),
	PAM_ERR_ENV(PAM_CRED_UNAVAIL),
	PAM_ERR_ENV(PAM_CRED_EXPIRED),
	PAM_ERR_ENV(PAM_CRED_ERR),
	PAM_ERR_ENV(PAM_ACCT_EXPIRED),
	PAM_ERR_ENV(PAM_AUTHTOK_EXPIRED),
	PAM_ERR_ENV(PAM_SESSION_ERR),
	PAM_ERR_ENV(PAM_AUTHTOK_ERR),
	PAM_ERR_ENV(PAM_AUTHTOK_RECOVERY_ERR),
	PAM_ERR_ENV(PAM_AUTHTOK_LOCK_BUSY),
	PAM_ERR_ENV(PAM_AUTHTOK_DISABLE_AGING),
	PAM_ERR_ENV(PAM_NO_MODULE_DATA),
	PAM_ERR_ENV(PAM_IGNORE),
	PAM_ERR_ENV(PAM_ABORT),
	PAM_ERR_ENV(PAM_TRY_AGAIN),
	PAM_ERR_ENV(PAM_MODULE_UNKNOWN),
	PAM_ERR_ENV(PAM_DOMAIN_UNKNOWN),
	PAM_ERR_ENV(PAM_NUM_ERR),
};
#define NUM_PAM_ERR_ENV (sizeof(pam_err_env) / sizeof(pam_err_env[0]))

struct pe_opts {
	int	return_prog_exit_status;
	int	capture_stdout;
	int	capture_stderr;
	int	expose_authtok;
};

static int
parse_options(const char *func, int *argc, const char **argv[],
    struct pe_opts *options)
{
	int i;

	/*
	 * Parse options:
	 *   return_prog_exit_status:
	 *     use the program exit status as the return code of pam_exec
	 *   --:
	 *     stop options parsing; what follows is the command to execute
	 */
	memset(options, 0, sizeof(*options));

	for (i = 0; i < *argc; ++i) {
		if (strcmp((*argv)[i], "debug") == 0 ||
		    strcmp((*argv)[i], "no_warn") == 0) {
			/* ignore */
		} else if (strcmp((*argv)[i], "capture_stdout") == 0) {
			options->capture_stdout = 1;
		} else if (strcmp((*argv)[i], "capture_stderr") == 0) {
			options->capture_stderr = 1;
		} else if (strcmp((*argv)[i], "return_prog_exit_status") == 0) {
			options->return_prog_exit_status = 1;
		} else if (strcmp((*argv)[i], "expose_authtok") == 0) {
			options->expose_authtok = 1;
		} else {
			if (strcmp((*argv)[i], "--") == 0) {
				(*argc)--;
				(*argv)++;
			}
			break;
		}
		openpam_log(PAM_LOG_DEBUG, "%s: option \"%s\" enabled",
		    func, (*argv)[i]);
	}

	(*argc) -= i;
	(*argv) += i;

	return (0);
}

static int
_pam_exec(pam_handle_t *pamh,
    const char *func, int flags __unused, int argc, const char *argv[],
    struct pe_opts *options)
{
	char buf[PAM_MAX_MSG_SIZE];
	struct pollfd pfd[4];
	const void *item;
	char **envlist, *envstr, *resp, **tmp;
	ssize_t rlen, wlen;
	int envlen, extralen, i;
	int pam_err, serrno, status;
	int chin[2], chout[2], cherr[2], pd;
	nfds_t nfds, nreadfds;
	pid_t pid;
	const char *authtok;
	size_t authtok_size;
	int rc;

	pd = -1;
	pid = 0;
	chin[0] = chin[1] = chout[0] = chout[1] = cherr[0] = cherr[1] = -1;
	envlist = NULL;

#define OUT(ret) do { pam_err = (ret); goto out; } while (0)

	/* Check there's a program name left after parsing options. */
	if (argc < 1) {
		openpam_log(PAM_LOG_ERROR, "%s: No program specified: aborting",
		    func);
		OUT(PAM_SERVICE_ERR);
	}

	/*
	 * Set up the child's environment list.  It consists of the PAM
	 * environment, a few hand-picked PAM items, the name of the
	 * service function, and if return_prog_exit_status is set, the
	 * numerical values of all PAM error codes.
	 */

	/* compute the final size of the environment. */
	envlist = pam_getenvlist(pamh);
	for (envlen = 0; envlist[envlen] != NULL; ++envlen)
		/* nothing */ ;
	extralen = NUM_PAM_ITEM_ENV + 1;
	if (options->return_prog_exit_status)
		extralen += NUM_PAM_ERR_ENV;
	tmp = reallocarray(envlist, envlen + extralen + 1, sizeof(*envlist));
	openpam_log(PAM_LOG_DEBUG, "envlen = %d extralen = %d tmp = %p",
	    envlen, extralen, tmp);
	if (tmp == NULL)
		OUT(PAM_BUF_ERR);
	envlist = tmp;
	extralen += envlen;

	/* copy selected PAM items to the environment */
	for (i = 0; i < NUM_PAM_ITEM_ENV; ++i) {
		pam_err = pam_get_item(pamh, pam_item_env[i].item, &item);
		if (pam_err != PAM_SUCCESS || item == NULL)
			continue;
		if (asprintf(&envstr, "%s=%s", pam_item_env[i].name, item) < 0)
			OUT(PAM_BUF_ERR);
		envlist[envlen++] = envstr;
		envlist[envlen] = NULL;
		openpam_log(PAM_LOG_DEBUG, "setenv %s", envstr);
	}

	/* add the name of the service function to the environment */
	if (asprintf(&envstr, "PAM_SM_FUNC=%s", func) < 0)
		OUT(PAM_BUF_ERR);
	envlist[envlen++] = envstr;
	envlist[envlen] = NULL;

	/* add the PAM error codes to the environment. */
	if (options->return_prog_exit_status) {
		for (i = 0; i < (int)NUM_PAM_ERR_ENV; ++i) {
			if ((envstr = strdup(pam_err_env[i])) == NULL)
				OUT(PAM_BUF_ERR);
			envlist[envlen++] = envstr;
			envlist[envlen] = NULL;
		}
	}

	openpam_log(PAM_LOG_DEBUG, "envlen = %d extralen = %d envlist = %p",
	    envlen, extralen, envlist);

	/* set up pipe and get authtok if requested */
	if (options->expose_authtok) {
		if (pipe(chin) != 0) {
			openpam_log(PAM_LOG_ERROR, "%s: pipe(): %m", func);
			OUT(PAM_SYSTEM_ERR);
		}
		if (fcntl(chin[1], F_SETFL, O_NONBLOCK)) {
			openpam_log(PAM_LOG_ERROR, "%s: fcntl(): %m", func);
			OUT(PAM_SYSTEM_ERR);
		}
		rc = pam_get_authtok(pamh, PAM_AUTHTOK, &authtok, NULL);
		if (rc == PAM_SUCCESS) {
			/* We include the trailing NUL-terminator. */
			authtok_size = strlen(authtok) + 1;
		} else {
			openpam_log(PAM_LOG_ERROR, "%s: pam_get_authtok(): %s", func,
						pam_strerror(pamh, rc));
			OUT(PAM_SYSTEM_ERR);
		}
	}
	/* set up pipes if capture was requested */
	if (options->capture_stdout) {
		if (pipe(chout) != 0) {
			openpam_log(PAM_LOG_ERROR, "%s: pipe(): %m", func);
			OUT(PAM_SYSTEM_ERR);
		}
		if (fcntl(chout[0], F_SETFL, O_NONBLOCK) != 0) {
			openpam_log(PAM_LOG_ERROR, "%s: fcntl(): %m", func);
			OUT(PAM_SYSTEM_ERR);
		}
	} else {
		if ((chout[1] = open("/dev/null", O_RDWR)) < 0) {
			openpam_log(PAM_LOG_ERROR, "%s: /dev/null: %m", func);
			OUT(PAM_SYSTEM_ERR);
		}
	}
	if (options->capture_stderr) {
		if (pipe(cherr) != 0) {
			openpam_log(PAM_LOG_ERROR, "%s: pipe(): %m", func);
			OUT(PAM_SYSTEM_ERR);
		}
		if (fcntl(cherr[0], F_SETFL, O_NONBLOCK) != 0) {
			openpam_log(PAM_LOG_ERROR, "%s: fcntl(): %m", func);
			OUT(PAM_SYSTEM_ERR);
		}
	} else {
		if ((cherr[1] = open("/dev/null", O_RDWR)) < 0) {
			openpam_log(PAM_LOG_ERROR, "%s: /dev/null: %m", func);
			OUT(PAM_SYSTEM_ERR);
		}
	}

	if ((pid = pdfork(&pd, 0)) == 0) {
		/* child */
		if ((chin[1] >= 0 && close(chin[1]) != 0) ||
			(chout[0] >= 0 && close(chout[0]) != 0) ||
		    (cherr[0] >= 0 && close(cherr[0]) != 0)) {
			openpam_log(PAM_LOG_ERROR, "%s: close(): %m", func);
		} else if (chin[0] >= 0 &&
			dup2(chin[0], STDIN_FILENO) != STDIN_FILENO) {
			openpam_log(PAM_LOG_ERROR, "%s: dup2(): %m", func);
		} else if (dup2(chout[1], STDOUT_FILENO) != STDOUT_FILENO ||
		    dup2(cherr[1], STDERR_FILENO) != STDERR_FILENO) {
			openpam_log(PAM_LOG_ERROR, "%s: dup2(): %m", func);
		} else {
			execve(argv[0], (char * const *)argv,
			    (char * const *)envlist);
			openpam_log(PAM_LOG_ERROR, "%s: execve(%s): %m",
			    func, argv[0]);
		}
		_exit(1);
	}
	/* parent */
	if (pid == -1) {
		openpam_log(PAM_LOG_ERROR, "%s: pdfork(): %m", func);
		OUT(PAM_SYSTEM_ERR);
	}
	/* use poll() to watch the process and stdin / stdout / stderr */
	if (chin[0] >= 0)
		close(chin[0]);
	if (chout[1] >= 0)
		close(chout[1]);
	if (cherr[1] >= 0)
		close(cherr[1]);
	memset(pfd, 0, sizeof pfd);
	pfd[0].fd = pd;
	pfd[0].events = POLLHUP;
	nfds = 1;
	nreadfds = 0;
	if (options->capture_stdout) {
		pfd[nfds].fd = chout[0];
		pfd[nfds].events = POLLIN|POLLERR|POLLHUP;
		nfds++;
		nreadfds++;
	}
	if (options->capture_stderr) {
		pfd[nfds].fd = cherr[0];
		pfd[nfds].events = POLLIN|POLLERR|POLLHUP;
		nfds++;
		nreadfds++;
	}
	if (options->expose_authtok) {
		pfd[nfds].fd = chin[1];
		pfd[nfds].events = POLLOUT|POLLERR|POLLHUP;
		nfds++;
	}

	/* loop until the process exits */
	do {
		if (poll(pfd, nfds, INFTIM) < 0) {
			openpam_log(PAM_LOG_ERROR, "%s: poll(): %m", func);
			OUT(PAM_SYSTEM_ERR);
		}
		/* are the stderr / stdout pipes ready for reading? */
		for (i = 1; i < 1 + nreadfds; ++i) {
			if ((pfd[i].revents & POLLIN) == 0)
				continue;
			if ((rlen = read(pfd[i].fd, buf, sizeof(buf) - 1)) < 0) {
				openpam_log(PAM_LOG_ERROR, "%s: read(): %m",
				    func);
				OUT(PAM_SYSTEM_ERR);
			} else if (rlen == 0) {
				continue;
			}
			buf[rlen] = '\0';
			(void)pam_prompt(pamh, pfd[i].fd == chout[0] ?
			    PAM_TEXT_INFO : PAM_ERROR_MSG, &resp, "%s", buf);
		}
		/* is the stdin pipe ready for writing? */
		if (options->expose_authtok && authtok_size > 0 &&
			(pfd[nfds - 1].revents & POLLOUT) != 0) {
			if ((wlen = write(chin[1], authtok, authtok_size)) < 0) {
				if (errno == EAGAIN)
					continue;
				openpam_log(PAM_LOG_ERROR, "%s: write(): %m",
				    func);
				OUT(PAM_SYSTEM_ERR);
			} else {
				authtok += wlen;
				authtok_size -= wlen;
				if (authtok_size == 0) {
					/* finished writing; close and forget the pipe */
					close(chin[1]);
					chin[1] = -1;
					nfds--;
				}
			}
		}
	} while (pfd[0].revents == 0);

	/* the child process has exited */
	while (waitpid(pid, &status, 0) == -1) {
		if (errno == EINTR)
			continue;
		openpam_log(PAM_LOG_ERROR, "%s: waitpid(): %m", func);
		OUT(PAM_SYSTEM_ERR);
	}

	/* check exit code */
	if (WIFSIGNALED(status)) {
		openpam_log(PAM_LOG_ERROR, "%s: %s caught signal %d%s",
		    func, argv[0], WTERMSIG(status),
		    WCOREDUMP(status) ? " (core dumped)" : "");
		OUT(PAM_SERVICE_ERR);
	}
	if (!WIFEXITED(status)) {
		openpam_log(PAM_LOG_ERROR, "%s: unknown status 0x%x",
		    func, status);
		OUT(PAM_SERVICE_ERR);
	}

	if (options->return_prog_exit_status) {
		openpam_log(PAM_LOG_DEBUG,
		    "%s: Use program exit status as return value: %d",
		    func, WEXITSTATUS(status));
		OUT(WEXITSTATUS(status));
	} else {
		OUT(WEXITSTATUS(status) == 0 ? PAM_SUCCESS : PAM_PERM_DENIED);
	}
	/* unreachable */
out:
	serrno = errno;
	if (pd >= 0)
		close(pd);
	if (chin[0] >= 0)
		close(chin[0]);
	if (chin[1] >= 0)
		close(chin[1]);
	if (chout[0] >= 0)
		close(chout[0]);
	if (chout[1] >= 0)
		close(chout[1]);
	if (cherr[0] >= 0)
		close(cherr[0]);
	if (cherr[0] >= 0)
		close(cherr[1]);
	if (envlist != NULL)
		openpam_free_envlist(envlist);
	errno = serrno;
	return (pam_err);	
}

PAM_EXTERN int
pam_sm_authenticate(pam_handle_t *pamh, int flags,
    int argc, const char *argv[])
{
	int ret;
	struct pe_opts options;

	ret = parse_options(__func__, &argc, &argv, &options);
	if (ret != 0)
		return (PAM_SERVICE_ERR);

	ret = _pam_exec(pamh, __func__, flags, argc, argv, &options);

	/*
	 * We must check that the program returned a valid code for this
	 * function.
	 */
	switch (ret) {
	case PAM_SUCCESS:
	case PAM_ABORT:
	case PAM_AUTHINFO_UNAVAIL:
	case PAM_AUTH_ERR:
	case PAM_BUF_ERR:
	case PAM_CONV_ERR:
	case PAM_CRED_INSUFFICIENT:
	case PAM_IGNORE:
	case PAM_MAXTRIES:
	case PAM_PERM_DENIED:
	case PAM_SERVICE_ERR:
	case PAM_SYSTEM_ERR:
	case PAM_USER_UNKNOWN:
		break;
	default:
		openpam_log(PAM_LOG_ERROR, "%s returned invalid code %d",
		    argv[0], ret);
		ret = PAM_SERVICE_ERR;
	}

	return (ret);
}

PAM_EXTERN int
pam_sm_setcred(pam_handle_t *pamh, int flags,
    int argc, const char *argv[])
{
	int ret;
	struct pe_opts options;

	ret = parse_options(__func__, &argc, &argv, &options);
	if (ret != 0)
		return (PAM_SERVICE_ERR);

	ret = _pam_exec(pamh, __func__, flags, argc, argv, &options);

	/*
	 * We must check that the program returned a valid code for this
	 * function.
	 */
	switch (ret) {
	case PAM_SUCCESS:
	case PAM_ABORT:
	case PAM_BUF_ERR:
	case PAM_CONV_ERR:
	case PAM_CRED_ERR:
	case PAM_CRED_EXPIRED:
	case PAM_CRED_UNAVAIL:
	case PAM_IGNORE:
	case PAM_PERM_DENIED:
	case PAM_SERVICE_ERR:
	case PAM_SYSTEM_ERR:
	case PAM_USER_UNKNOWN:
		break;
	default:
		openpam_log(PAM_LOG_ERROR, "%s returned invalid code %d",
		    argv[0], ret);
		ret = PAM_SERVICE_ERR;
	}

	return (ret);
}

PAM_EXTERN int
pam_sm_acct_mgmt(pam_handle_t *pamh, int flags,
    int argc, const char *argv[])
{
	int ret;
	struct pe_opts options;

	ret = parse_options(__func__, &argc, &argv, &options);
	if (ret != 0)
		return (PAM_SERVICE_ERR);

	ret = _pam_exec(pamh, __func__, flags, argc, argv, &options);

	/*
	 * We must check that the program returned a valid code for this
	 * function.
	 */
	switch (ret) {
	case PAM_SUCCESS:
	case PAM_ABORT:
	case PAM_ACCT_EXPIRED:
	case PAM_AUTH_ERR:
	case PAM_BUF_ERR:
	case PAM_CONV_ERR:
	case PAM_IGNORE:
	case PAM_NEW_AUTHTOK_REQD:
	case PAM_PERM_DENIED:
	case PAM_SERVICE_ERR:
	case PAM_SYSTEM_ERR:
	case PAM_USER_UNKNOWN:
		break;
	default:
		openpam_log(PAM_LOG_ERROR, "%s returned invalid code %d",
		    argv[0], ret);
		ret = PAM_SERVICE_ERR;
	}

	return (ret);
}

PAM_EXTERN int
pam_sm_open_session(pam_handle_t *pamh, int flags,
    int argc, const char *argv[])
{
	int ret;
	struct pe_opts options;

	ret = parse_options(__func__, &argc, &argv, &options);
	if (ret != 0)
		return (PAM_SERVICE_ERR);

	ret = _pam_exec(pamh, __func__, flags, argc, argv, &options);

	/*
	 * We must check that the program returned a valid code for this
	 * function.
	 */
	switch (ret) {
	case PAM_SUCCESS:
	case PAM_ABORT:
	case PAM_BUF_ERR:
	case PAM_CONV_ERR:
	case PAM_IGNORE:
	case PAM_PERM_DENIED:
	case PAM_SERVICE_ERR:
	case PAM_SESSION_ERR:
	case PAM_SYSTEM_ERR:
		break;
	default:
		openpam_log(PAM_LOG_ERROR, "%s returned invalid code %d",
		    argv[0], ret);
		ret = PAM_SERVICE_ERR;
	}

	return (ret);
}

PAM_EXTERN int
pam_sm_close_session(pam_handle_t *pamh, int flags,
    int argc, const char *argv[])
{
	int ret;
	struct pe_opts options;

	ret = parse_options(__func__, &argc, &argv, &options);
	if (ret != 0)
		return (PAM_SERVICE_ERR);

	ret = _pam_exec(pamh, __func__, flags, argc, argv, &options);

	/*
	 * We must check that the program returned a valid code for this
	 * function.
	 */
	switch (ret) {
	case PAM_SUCCESS:
	case PAM_ABORT:
	case PAM_BUF_ERR:
	case PAM_CONV_ERR:
	case PAM_IGNORE:
	case PAM_PERM_DENIED:
	case PAM_SERVICE_ERR:
	case PAM_SESSION_ERR:
	case PAM_SYSTEM_ERR:
		break;
	default:
		openpam_log(PAM_LOG_ERROR, "%s returned invalid code %d",
		    argv[0], ret);
		ret = PAM_SERVICE_ERR;
	}

	return (ret);
}

PAM_EXTERN int
pam_sm_chauthtok(pam_handle_t *pamh, int flags,
    int argc, const char *argv[])
{
	int ret;
	struct pe_opts options;

	ret = parse_options(__func__, &argc, &argv, &options);
	if (ret != 0)
		return (PAM_SERVICE_ERR);

	ret = _pam_exec(pamh, __func__, flags, argc, argv, &options);

	/*
	 * We must check that the program returned a valid code for this
	 * function.
	 */
	switch (ret) {
	case PAM_SUCCESS:
	case PAM_ABORT:
	case PAM_AUTHTOK_DISABLE_AGING:
	case PAM_AUTHTOK_ERR:
	case PAM_AUTHTOK_LOCK_BUSY:
	case PAM_AUTHTOK_RECOVERY_ERR:
	case PAM_BUF_ERR:
	case PAM_CONV_ERR:
	case PAM_IGNORE:
	case PAM_PERM_DENIED:
	case PAM_SERVICE_ERR:
	case PAM_SYSTEM_ERR:
	case PAM_TRY_AGAIN:
		break;
	default:
		openpam_log(PAM_LOG_ERROR, "%s returned invalid code %d",
		    argv[0], ret);
		ret = PAM_SERVICE_ERR;
	}

	return (ret);
}

PAM_MODULE_ENTRY("pam_exec");
