/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2003 Networks Associates Technology, Inc.
 * Copyright (c) 2004-2011 Dag-Erling Smørgrav
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

#include <sys/param.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define PAM_SM_AUTH
#define PAM_SM_SESSION

#include <security/pam_appl.h>
#include <security/pam_modules.h>
#include <security/openpam.h>

#include <openssl/evp.h>

#define __bounded__(x, y, z)
#include "authfd.h"
#include "authfile.h"
#include "sshkey.h"

#define ssh_add_identity(auth, key, comment) \
	ssh_add_identity_constrained(auth, key, comment, 0, 0, 0)

extern char **environ;

struct pam_ssh_key {
	struct sshkey	*key;
	char		*comment;
};

static const char *pam_ssh_prompt = "SSH passphrase: ";
static const char *pam_ssh_have_keys = "pam_ssh_have_keys";

static const char *pam_ssh_keyfiles[] = {
	".ssh/id_rsa",		/* SSH2 RSA key */
	".ssh/id_dsa",		/* SSH2 DSA key */
	".ssh/id_ecdsa",	/* SSH2 ECDSA key */
	".ssh/id_ed25519",	/* SSH2 Ed25519 key */
	NULL
};

static const char *pam_ssh_agent = "/usr/bin/ssh-agent";
static char str_ssh_agent[] = "ssh-agent";
static char str_dash_s[] = "-s";
static char *const pam_ssh_agent_argv[] = { str_ssh_agent, str_dash_s, NULL };
static char *const pam_ssh_agent_envp[] = { NULL };

/*
 * Attempts to load a private key from the specified file in the specified
 * directory, using the specified passphrase.  If successful, returns a
 * struct pam_ssh_key containing the key and its comment.
 */
static struct pam_ssh_key *
pam_ssh_load_key(const char *dir, const char *kfn, const char *passphrase,
    int nullok)
{
	char fn[PATH_MAX];
	struct pam_ssh_key *psk;
	struct sshkey *key;
	char *comment;
	int ret;

	if (snprintf(fn, sizeof(fn), "%s/%s", dir, kfn) > (int)sizeof(fn))
		return (NULL);
	/*
	 * If the key is unencrypted, OpenSSL ignores the passphrase, so
	 * it will seem like the user typed in the right one.  This allows
	 * a user to circumvent nullok by providing a dummy passphrase.
	 * Verify that the key really *is* encrypted by trying to load it
	 * with an empty passphrase, and if the key is not encrypted,
	 * accept only an empty passphrase.
	 */
	ret = sshkey_load_private(fn, "", &key, &comment);
	if (ret == 0 && !(*passphrase == '\0' && nullok)) {
		sshkey_free(key);
		return (NULL);
	}
	if (ret != 0)
		ret = sshkey_load_private(fn, passphrase, &key, &comment);
	if (ret != 0) {
		openpam_log(PAM_LOG_DEBUG, "failed to load key from %s", fn);
		return (NULL);
	}

	openpam_log(PAM_LOG_DEBUG, "loaded '%s' from %s", comment, fn);
	if ((psk = malloc(sizeof(*psk))) == NULL) {
		sshkey_free(key);
		free(comment);
		return (NULL);
	}
	psk->key = key;
	psk->comment = comment;
	return (psk);
}

/*
 * Wipes a private key and frees the associated resources.
 */
static void
pam_ssh_free_key(pam_handle_t *pamh __unused,
    void *data, int pam_err __unused)
{
	struct pam_ssh_key *psk;

	psk = data;
	sshkey_free(psk->key);
	free(psk->comment);
	free(psk);
}

PAM_EXTERN int
pam_sm_authenticate(pam_handle_t *pamh, int flags __unused,
    int argc __unused, const char *argv[] __unused)
{
	const char **kfn, *passphrase, *user;
	const void *item;
	struct passwd *pwd;
	struct pam_ssh_key *psk;
	int nkeys, nullok, pam_err, pass;

	nullok = (openpam_get_option(pamh, "nullok") != NULL);

	/* PEM is not loaded by default */
	OpenSSL_add_all_algorithms();

	/* get user name and home directory */
	pam_err = pam_get_user(pamh, &user, NULL);
	if (pam_err != PAM_SUCCESS)
		return (pam_err);
	pwd = getpwnam(user);
	if (pwd == NULL)
		return (PAM_USER_UNKNOWN);
	if (pwd->pw_dir == NULL)
		return (PAM_AUTH_ERR);

	nkeys = 0;
	pass = (pam_get_item(pamh, PAM_AUTHTOK, &item) == PAM_SUCCESS &&
	    item != NULL);
 load_keys:
	/* get passphrase */
	pam_err = pam_get_authtok(pamh, PAM_AUTHTOK,
	    &passphrase, pam_ssh_prompt);
	if (pam_err != PAM_SUCCESS)
		return (pam_err);

	/* switch to user credentials */
	pam_err = openpam_borrow_cred(pamh, pwd);
	if (pam_err != PAM_SUCCESS)
		return (pam_err);

	/* try to load keys from all keyfiles we know of */
	for (kfn = pam_ssh_keyfiles; *kfn != NULL; ++kfn) {
		psk = pam_ssh_load_key(pwd->pw_dir, *kfn, passphrase, nullok);
		if (psk != NULL) {
			pam_set_data(pamh, *kfn, psk, pam_ssh_free_key);
			++nkeys;
		}
	}

	/* switch back to arbitrator credentials */
	openpam_restore_cred(pamh);

	/*
	 * If we tried an old token and didn't get anything, and
	 * try_first_pass was specified, try again after prompting the
	 * user for a new passphrase.
	 */
	if (nkeys == 0 && pass == 1 &&
	    openpam_get_option(pamh, "try_first_pass") != NULL) {
		pam_set_item(pamh, PAM_AUTHTOK, NULL);
		pass = 0;
		goto load_keys;
	}

	/* no keys? */
	if (nkeys == 0)
		return (PAM_AUTH_ERR);

	pam_set_data(pamh, pam_ssh_have_keys, NULL, NULL);
	return (PAM_SUCCESS);
}

PAM_EXTERN int
pam_sm_setcred(pam_handle_t *pamh __unused, int flags __unused,
    int argc __unused, const char *argv[] __unused)
{

	return (PAM_SUCCESS);
}

/*
 * Parses a line from ssh-agent's output.
 */
static void
pam_ssh_process_agent_output(pam_handle_t *pamh, FILE *f)
{
	char *line, *p, *key, *val;
	size_t len;

	while ((line = fgetln(f, &len)) != NULL) {
		if (len < 4 || strncmp(line, "SSH_", 4) != 0)
			continue;

		/* find equal sign at end of key */
		for (p = key = line; p < line + len; ++p)
			if (*p == '=')
				break;
		if (p == line + len || *p != '=')
			continue;
		*p = '\0';

		/* find semicolon at end of value */
		for (val = ++p; p < line + len; ++p)
			if (*p == ';')
				break;
		if (p == line + len || *p != ';')
			continue;
		*p = '\0';

		/* store key-value pair in environment */
		openpam_log(PAM_LOG_DEBUG, "got %s: %s", key, val);
		pam_setenv(pamh, key, val, 1);
	}
}

/*
 * Starts an ssh agent and stores the environment variables derived from
 * its output.
 */
static int
pam_ssh_start_agent(pam_handle_t *pamh)
{
	int agent_pipe[2];
	pid_t pid;
	FILE *f;

	/* get a pipe which we will use to read the agent's output */
	if (pipe(agent_pipe) == -1)
		return (PAM_SYSTEM_ERR);

	/* start the agent */
	openpam_log(PAM_LOG_DEBUG, "starting an ssh agent");
	pid = fork();
	if (pid == (pid_t)-1) {
		/* failed */
		close(agent_pipe[0]);
		close(agent_pipe[1]);
		return (PAM_SYSTEM_ERR);
	}
	if (pid == 0) {
		int fd;

		/* child: drop privs, close fds and start agent */
		setgid(getegid());
		setuid(geteuid());
		close(STDIN_FILENO);
		open(_PATH_DEVNULL, O_RDONLY);
		dup2(agent_pipe[1], STDOUT_FILENO);
		dup2(agent_pipe[1], STDERR_FILENO);
		for (fd = 3; fd < getdtablesize(); ++fd)
			close(fd);
		execve(pam_ssh_agent, pam_ssh_agent_argv, pam_ssh_agent_envp);
		_exit(127);
	}

	/* parent */
	close(agent_pipe[1]);
	if ((f = fdopen(agent_pipe[0], "r")) == NULL)
		return (PAM_SYSTEM_ERR);
	pam_ssh_process_agent_output(pamh, f);
	fclose(f);

	return (PAM_SUCCESS);
}

/*
 * Adds previously stored keys to a running agent.
 */
static int
pam_ssh_add_keys_to_agent(pam_handle_t *pamh)
{
	const struct pam_ssh_key *psk;
	const char **kfn;
	const void *item;
	char **envlist, **env;
	int fd, pam_err;

	/* switch to PAM environment */
	envlist = environ;
	if ((environ = pam_getenvlist(pamh)) == NULL) {
		environ = envlist;
		return (PAM_SYSTEM_ERR);
	}

	/* get a connection to the agent */
	if (ssh_get_authentication_socket(&fd) != 0) {
		openpam_log(PAM_LOG_DEBUG, "failed to connect to the agent");
		pam_err = PAM_SYSTEM_ERR;
		goto end;
	}

	/* look for keys to add to it */
	for (kfn = pam_ssh_keyfiles; *kfn != NULL; ++kfn) {
		pam_err = pam_get_data(pamh, *kfn, &item);
		if (pam_err == PAM_SUCCESS && item != NULL) {
			psk = item;
			if (ssh_add_identity(fd, psk->key, psk->comment) == 0)
				openpam_log(PAM_LOG_DEBUG,
				    "added %s to ssh agent", psk->comment);
			else
				openpam_log(PAM_LOG_DEBUG, "failed "
				    "to add %s to ssh agent", psk->comment);
			/* we won't need the key again, so wipe it */
			pam_set_data(pamh, *kfn, NULL, NULL);
		}
	}
	pam_err = PAM_SUCCESS;

	/* disconnect from agent */
	ssh_close_authentication_socket(fd);

 end:
	/* switch back to original environment */
	for (env = environ; *env != NULL; ++env)
		free(*env);
	free(environ);
	environ = envlist;

	return (pam_err);
}

PAM_EXTERN int
pam_sm_open_session(pam_handle_t *pamh, int flags __unused,
    int argc __unused, const char *argv[] __unused)
{
	struct passwd *pwd;
	const char *user;
	const void *data;
	int pam_err;

	/* no keys, no work */
	if (pam_get_data(pamh, pam_ssh_have_keys, &data) != PAM_SUCCESS &&
	    openpam_get_option(pamh, "want_agent") == NULL)
		return (PAM_SUCCESS);

	/* switch to user credentials */
	pam_err = pam_get_user(pamh, &user, NULL);
	if (pam_err != PAM_SUCCESS)
		return (pam_err);
	pwd = getpwnam(user);
	if (pwd == NULL)
		return (PAM_USER_UNKNOWN);
	pam_err = openpam_borrow_cred(pamh, pwd);
	if (pam_err != PAM_SUCCESS)
		return (pam_err);

	/* start the agent */
	pam_err = pam_ssh_start_agent(pamh);
	if (pam_err != PAM_SUCCESS) {
		openpam_restore_cred(pamh);
		return (pam_err);
	}

	/* we have an agent, see if we can add any keys to it */
	pam_err = pam_ssh_add_keys_to_agent(pamh);
	if (pam_err != PAM_SUCCESS) {
		/* XXX ignore failures */
	}

	openpam_restore_cred(pamh);
	return (PAM_SUCCESS);
}

PAM_EXTERN int
pam_sm_close_session(pam_handle_t *pamh, int flags __unused,
    int argc __unused, const char *argv[] __unused)
{
	const char *ssh_agent_pid;
	char *end;
	int status;
	pid_t pid;

	if ((ssh_agent_pid = pam_getenv(pamh, "SSH_AGENT_PID")) == NULL) {
		openpam_log(PAM_LOG_DEBUG, "no ssh agent");
		return (PAM_SUCCESS);
	}
	pid = (pid_t)strtol(ssh_agent_pid, &end, 10);
	if (*ssh_agent_pid == '\0' || *end != '\0') {
		openpam_log(PAM_LOG_DEBUG, "invalid ssh agent pid");
		return (PAM_SESSION_ERR);
	}
	openpam_log(PAM_LOG_DEBUG, "killing ssh agent %d", (int)pid);
	if (kill(pid, SIGTERM) == -1 ||
	    (waitpid(pid, &status, 0) == -1 && errno != ECHILD))
		return (PAM_SYSTEM_ERR);
	return (PAM_SUCCESS);
}

PAM_MODULE_ENTRY("pam_ssh");
