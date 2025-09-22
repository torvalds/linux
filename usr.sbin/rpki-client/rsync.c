/*	$OpenBSD: rsync.c,v 1.59 2025/08/01 13:46:06 claudio Exp $ */
/*
 * Copyright (c) 2019 Kristaps Dzonsons <kristaps@bsd.lv>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <assert.h>
#include <err.h>
#include <errno.h>
#include <poll.h>
#include <resolv.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <imsg.h>

#include "extern.h"

#define	__STRINGIFY(x)	#x
#define	STRINGIFY(x)	__STRINGIFY(x)

/*
 * A running rsync process.
 * We can have multiple of these simultaneously and need to keep track
 * of which process maps to which request.
 */
struct rsync {
	TAILQ_ENTRY(rsync)	 entry;
	char			*uri; /* uri of this rsync proc */
	char			*dst; /* destination directory */
	char			*compdst; /* compare against directory */
	unsigned int		 id; /* identity of request */
	pid_t			 pid; /* pid of process or 0 if unassociated */
	int			 term_sent;
};

static TAILQ_HEAD(, rsync)	states = TAILQ_HEAD_INITIALIZER(states);

/*
 * Return the base of a rsync URI (rsync://hostname/module). The
 * caRepository provided by the RIR CAs point deeper than they should
 * which would result in many rsync calls for almost every subdirectory.
 * This is inefficient so instead crop the URI to a common base.
 * The returned string needs to be freed by the caller.
 */
char *
rsync_base_uri(const char *uri)
{
	const char *host, *module, *rest;
	char *base_uri;

	/* Case-insensitive rsync URI. */
	if (strncasecmp(uri, RSYNC_PROTO, RSYNC_PROTO_LEN) != 0) {
		warnx("%s: not using rsync schema", uri);
		return NULL;
	}

	/* Parse the non-zero-length hostname. */
	host = uri + RSYNC_PROTO_LEN;

	if ((module = strchr(host, '/')) == NULL) {
		warnx("%s: missing rsync module", uri);
		return NULL;
	} else if (module == host) {
		warnx("%s: zero-length rsync host", uri);
		return NULL;
	}

	/* The non-zero-length module follows the hostname. */
	module++;
	if (*module == '\0') {
		warnx("%s: zero-length rsync module", uri);
		return NULL;
	}

	/* The path component is optional. */
	if ((rest = strchr(module, '/')) == NULL) {
		if ((base_uri = strdup(uri)) == NULL)
			err(1, NULL);
		return base_uri;
	} else if (rest == module) {
		warnx("%s: zero-length module", uri);
		return NULL;
	}

	if ((base_uri = strndup(uri, rest - uri)) == NULL)
		err(1, NULL);
	return base_uri;
}

/*
 * The directory passed as --compare-dest needs to be relative to
 * the destination directory. This function takes care of that.
 */
static char *
rsync_fixup_dest(char *destdir, char *compdir)
{
	const char *dotdot = "../../../../../../";	/* should be enough */
	int dirs = 1;
	char *fn;
	char c;

	while ((c = *destdir++) != '\0')
		if (c == '/')
			dirs++;

	if (dirs > 6)
		/* too deep for us */
		return NULL;

	if ((asprintf(&fn, "%.*s%s", dirs * 3, dotdot, compdir)) == -1)
		err(1, NULL);
	return fn;
}

static pid_t
exec_rsync(const char *prog, const char *bind_addr, char *uri, char *dst,
    char *compdst)
{
	pid_t pid;
	char *args[32];
	char *reldst;
	int i;

	if ((pid = fork()) == -1)
		err(1, "fork");

	if (pid == 0) {
		if (pledge("stdio exec", NULL) == -1)
			err(1, "pledge");
		i = 0;
		args[i++] = (char *)prog;
		args[i++] = "-rtO";
		args[i++] = "--no-motd";
		args[i++] = "--min-size=" STRINGIFY(MIN_FILE_SIZE);
		args[i++] = "--max-size=" STRINGIFY(MAX_FILE_SIZE);
		args[i++] = "--contimeout=" STRINGIFY(MAX_CONN_TIMEOUT);
		args[i++] = "--timeout=" STRINGIFY(MAX_IO_TIMEOUT);
		args[i++] = "--include=*/";
		args[i++] = "--include=*.cer";
		args[i++] = "--include=*.crl";
		args[i++] = "--include=*.gbr";
		args[i++] = "--include=*.mft";
		args[i++] = "--include=*.roa";
		args[i++] = "--include=*.asa";
		args[i++] = "--include=*.tak";
		args[i++] = "--include=*.spl";
		args[i++] = "--exclude=*";
		if (bind_addr != NULL) {
			args[i++] = "--address";
			args[i++] = (char *)bind_addr;
		}
		if (compdst != NULL &&
		    (reldst = rsync_fixup_dest(dst, compdst)) != NULL) {
			args[i++] = "--compare-dest";
			args[i++] = reldst;
		}
		args[i++] = uri;
		args[i++] = dst;
		args[i] = NULL;
		/* XXX args overflow not prevented */
		execvp(args[0], args);
		err(1, "%s: execvp", prog);
	}

	return pid;
}

static void
rsync_new(unsigned int id, char *uri, char *dst, char *compdst)
{
	struct rsync *s;

	if ((s = calloc(1, sizeof(*s))) == NULL)
		err(1, NULL);

	s->id = id;
	s->uri = uri;
	s->dst = dst;
	s->compdst = compdst;

	TAILQ_INSERT_TAIL(&states, s, entry);
}

static void
rsync_free(struct rsync *s)
{
	TAILQ_REMOVE(&states, s, entry);
	free(s->uri);
	free(s->dst);
	free(s->compdst);
	free(s);
}

static int
rsync_status(struct rsync *s, int st, int *rc)
{
	if (WIFEXITED(st)) {
		if (WEXITSTATUS(st) == 0)
			return 1;
		warnx("rsync %s failed", s->uri);
	} else if (WIFSIGNALED(st)) {
		warnx("rsync %s terminated; signal %d", s->uri, WTERMSIG(st));
		if (!s->term_sent || WTERMSIG(st) != SIGTERM)
			*rc = 1;
	} else {	/* should not be possible */
		warnx("rsync %s terminated abnormally", s->uri);
		*rc = 1;
	}
	return 0;
}

static void
proc_child(int signal)
{
	/* Nothing: just discard. */
}

/*
 * Process used for synchronising repositories.
 * This simply waits to be told which repository to synchronise, then
 * does so.
 * It then responds with the identifier of the repo that it updated.
 * It only exits cleanly when fd is closed.
 */
void
proc_rsync(char *prog, char *bind_addr, int fd)
{
	int			 nprocs = 0, npending = 0, rc = 0;
	struct pollfd		 pfd;
	struct msgbuf		*msgq;
	struct ibuf		*b;
	sigset_t		 mask, oldmask;
	struct rsync		*s, *ns;

	if (pledge("stdio rpath proc exec unveil", NULL) == -1)
		err(1, "pledge");

	if ((msgq = msgbuf_new_reader(sizeof(size_t), io_parse_hdr, NULL)) ==
	    NULL)
		err(1, NULL);
	pfd.fd = fd;

	/*
	 * Unveil the command we want to run.
	 * If this has a pathname component in it, interpret as a file
	 * and unveil the file directly.
	 * Otherwise, look up the command in our PATH.
	 */

	if (strchr(prog, '/') == NULL) {
		const char *pp;
		char *save, *cmd, *path;
		struct stat stt;

		if (getenv("PATH") == NULL)
			errx(1, "PATH is unset");
		if ((path = strdup(getenv("PATH"))) == NULL)
			err(1, NULL);
		save = path;
		while ((pp = strsep(&path, ":")) != NULL) {
			if (*pp == '\0')
				continue;
			if (asprintf(&cmd, "%s/%s", pp, prog) == -1)
				err(1, NULL);
			if (lstat(cmd, &stt) == -1) {
				free(cmd);
				continue;
			} else if (unveil(cmd, "x") == -1)
				err(1, "%s: unveil", cmd);
			free(cmd);
			break;
		}
		free(save);
	} else if (unveil(prog, "x") == -1)
		err(1, "%s: unveil", prog);

	if (pledge("stdio proc exec", NULL) == -1)
		err(1, "pledge");

	/* Initialise retriever for children exiting. */

	if (sigemptyset(&mask) == -1)
		err(1, NULL);
	if (signal(SIGCHLD, proc_child) == SIG_ERR)
		err(1, NULL);
	if (sigaddset(&mask, SIGCHLD) == -1)
		err(1, NULL);
	if (sigprocmask(SIG_BLOCK, &mask, &oldmask) == -1)
		err(1, NULL);

	for (;;) {
		char *uri, *dst, *compdst;
		unsigned int id;
		pid_t pid;
		int st;

		pfd.events = 0;
		pfd.events |= POLLIN;
		if (msgbuf_queuelen(msgq) > 0)
			pfd.events |= POLLOUT;

		if (npending > 0 && nprocs < MAX_RSYNC_REQUESTS) {
			TAILQ_FOREACH(s, &states, entry) {
				if (s->pid == 0) {
					s->pid = exec_rsync(prog, bind_addr,
					    s->uri, s->dst, s->compdst);
					if (++nprocs >= MAX_RSYNC_REQUESTS)
						break;
					if (--npending == 0)
						break;
				}
			}
		}

		if (ppoll(&pfd, 1, NULL, &oldmask) == -1) {
			if (errno != EINTR)
				err(1, "ppoll");

			/*
			 * If we've received an EINTR, it means that one
			 * of our children has exited and we can reap it
			 * and look up its identifier.
			 * Then we respond to the parent.
			 */

			while ((pid = waitpid(WAIT_ANY, &st, WNOHANG)) > 0) {
				int ok;

				TAILQ_FOREACH(s, &states, entry)
					if (s->pid == pid)
						break;
				if (s == NULL)
					errx(1, "waitpid: %d unexpected", pid);

				ok = rsync_status(s, st, &rc);

				b = io_new_buffer();
				io_simple_buffer(b, &s->id, sizeof(s->id));
				io_simple_buffer(b, &ok, sizeof(ok));
				io_close_buffer(msgq, b);

				rsync_free(s);
				nprocs--;
			}
			if (pid == -1 && errno != ECHILD)
				err(1, "waitpid");

			continue;
		}

		if (pfd.revents & POLLOUT) {
			if (msgbuf_write(fd, msgq) == -1) {
				if (errno == EPIPE)
					errx(1, "write: connection closed");
				else
					err(1, "write");
			}
		}

		/* connection closed */
		if (pfd.revents & POLLHUP)
			break;

		if (!(pfd.revents & POLLIN))
			continue;

		switch (ibuf_read(fd, msgq)) {
		case -1:
			err(1, "ibuf_read");
		case 0:
			errx(1, "ibuf_read: connection closed");
		}

		while ((b = io_buf_get(msgq)) != NULL) {
			/* Read host and module. */
			io_read_buf(b, &id, sizeof(id));
			io_read_opt_str(b, &dst);
			io_read_opt_str(b, &compdst);
			io_read_opt_str(b, &uri);

			ibuf_free(b);

			if (dst != NULL) {
				rsync_new(id, uri, dst, compdst);
				npending++;
			} else {
				assert(compdst == NULL);
				assert(uri == NULL);
				TAILQ_FOREACH(s, &states, entry)
					if (s->id == id)
						break;
				if (s != NULL) {
					if (s->pid != 0) {
						kill(s->pid, SIGTERM);
						s->term_sent = 1;
					} else
						rsync_free(s);
				}
			}
		}
	}

	/* No need for these to be hanging around. */
	TAILQ_FOREACH_SAFE(s, &states, entry, ns) {
		if (s->pid != 0)
			kill(s->pid, SIGTERM);
		rsync_free(s);
	}

	msgbuf_free(msgq);
	exit(rc);
}
