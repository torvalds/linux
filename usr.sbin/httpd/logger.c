/*	$OpenBSD: logger.c,v 1.25 2024/01/17 08:22:40 claudio Exp $	*/

/*
 * Copyright (c) 2014 Reyk Floeter <reyk@openbsd.org>
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

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/uio.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <imsg.h>

#include "httpd.h"

int		 logger_dispatch_parent(int, struct privsep_proc *,
		    struct imsg *);
int		 logger_dispatch_server(int, struct privsep_proc *,
		    struct imsg *);
void		 logger_shutdown(void);
void		 logger_close(void);
struct log_file *logger_open_file(const char *);
int		 logger_open_fd(struct imsg *);
int		 logger_open(struct server *, struct server_config *, void *);
void		 logger_init(struct privsep *, struct privsep_proc *p, void *);
int		 logger_start(void);
int		 logger_log(struct imsg *);

static uint32_t		 last_log_id = 0;

struct log_files log_files;

static struct privsep_proc procs[] = {
	{ "parent",	PROC_PARENT,	logger_dispatch_parent },
	{ "server",	PROC_SERVER,	logger_dispatch_server }
};

void
logger(struct privsep *ps, struct privsep_proc *p)
{
	proc_run(ps, p, procs, nitems(procs), logger_init, NULL);
}

void
logger_shutdown(void)
{
	logger_close();
	config_purge(httpd_env, CONFIG_ALL);
}

void
logger_init(struct privsep *ps, struct privsep_proc *p, void *arg)
{
	if (pledge("stdio recvfd", NULL) == -1)
		fatal("pledge");

	if (config_init(ps->ps_env) == -1)
		fatal("failed to initialize configuration");

	/* We use a custom shutdown callback */
	p->p_shutdown = logger_shutdown;

	TAILQ_INIT(&log_files);
}

void
logger_close(void)
{
	struct log_file	*log, *next;

	TAILQ_FOREACH_SAFE(log, &log_files, log_entry, next) {
		if (log->log_fd != -1) {
			close(log->log_fd);
			log->log_fd = -1;
		}
		TAILQ_REMOVE(&log_files, log, log_entry);
		free(log);
	}
}

struct log_file *
logger_open_file(const char *name)
{
	struct log_file	*log;
	struct iovec	 iov[2];

	if ((log = calloc(1, sizeof(*log))) == NULL) {
		log_warn("failed to allocate log %s", name);
		return (NULL);
	}

	log->log_id = ++last_log_id;
	(void)strlcpy(log->log_name, name, sizeof(log->log_name));

	/* The file will be opened by the parent process */
	log->log_fd = -1;

	iov[0].iov_base = &log->log_id;
	iov[0].iov_len = sizeof(log->log_id);
	iov[1].iov_base = log->log_name;
	iov[1].iov_len = strlen(log->log_name) + 1;

	if (proc_composev(httpd_env->sc_ps, PROC_PARENT, IMSG_LOG_OPEN,
	    iov, 2) != 0) {
		log_warn("%s: failed to compose IMSG_LOG_OPEN imsg", __func__);
		goto err;
	}

	TAILQ_INSERT_TAIL(&log_files, log, log_entry);

	return (log);

err:
	free(log);

	return (NULL);
}

int
logger_open_fd(struct imsg *imsg)
{
	struct log_file		*log;
	uint32_t		 id;

	IMSG_SIZE_CHECK(imsg, &id);
	memcpy(&id, imsg->data, sizeof(id));

	TAILQ_FOREACH(log, &log_files, log_entry) {
		if (log->log_id == id) {
			log->log_fd = imsg_get_fd(imsg);
			DPRINTF("%s: received log fd %d, file %s",
			    __func__, log->log_fd, log->log_name);
			return (0);
		}
	}

	return (-1);
}

int
logger_open_priv(struct imsg *imsg)
{
	char			 path[PATH_MAX];
	char			 name[PATH_MAX], *p;
	uint32_t		 id;
	size_t			 len;
	int			 fd;

	/* called from the privileged process */
	IMSG_SIZE_CHECK(imsg, &id);
	memcpy(&id, imsg->data, sizeof(id));
	p = (char *)imsg->data + sizeof(id);

	if ((size_t)snprintf(name, sizeof(name), "/%s", p) >= sizeof(name))
		return (-1);
	if ((len = strlcpy(path, httpd_env->sc_logdir, sizeof(path)))
	    >= sizeof(path))
		return (-1);

	p = path + len;
	len = sizeof(path) - len;

	if (canonicalize_path(name, p, len) == NULL) {
		log_warnx("invalid log name");
		return (-1);
	}

	if ((fd = open(path, O_WRONLY|O_APPEND|O_CREAT, 0644)) == -1) {
		log_warn("failed to open %s", path);
		return (-1);
	}

	proc_compose_imsg(httpd_env->sc_ps, PROC_LOGGER, -1,
	    IMSG_LOG_OPEN, -1, fd, &id, sizeof(id));

	DPRINTF("%s: opened log file %s, fd %d", __func__, path, fd);

	return (0);
}

int
logger_open(struct server *srv, struct server_config *srv_conf, void *arg)
{
	struct log_file	*log, *logfile = NULL, *errfile = NULL;

	if (srv_conf->flags & (SRVFLAG_SYSLOG | SRVFLAG_NO_LOG))
		return (0);

	/* disassociate */
	srv_conf->logaccess = srv_conf->logerror = NULL;

	TAILQ_FOREACH(log, &log_files, log_entry) {
		if (strcmp(log->log_name, srv_conf->accesslog) == 0)
			logfile = log;
		if (strcmp(log->log_name, srv_conf->errorlog) == 0)
			errfile = log;
	}

	if (logfile == NULL) {
		if ((srv_conf->logaccess =
		    logger_open_file(srv_conf->accesslog)) == NULL)
			return (-1);
	} else
		srv_conf->logaccess = logfile;

	if (errfile == NULL) {
		if ((srv_conf->logerror =
		    logger_open_file(srv_conf->errorlog)) == NULL)
			return (-1);
	} else
		srv_conf->logerror = errfile;

	return (0);
}

int
logger_start(void)
{
	logger_close();
	if (server_foreach(logger_open, NULL) == -1)
		fatalx("failed to open log files");
	return (0);
}

int
logger_log(struct imsg *imsg)
{
	char			*logline;
	uint32_t		 id;
	struct server_config	*srv_conf;
	struct log_file		*log;

	IMSG_SIZE_CHECK(imsg, &id);
	memcpy(&id, imsg->data, sizeof(id));

	if ((srv_conf = serverconfig_byid(id)) == NULL)
		fatalx("invalid logging requestr");

	if (imsg->hdr.type == IMSG_LOG_ACCESS)
		log = srv_conf->logaccess;
	else
		log = srv_conf->logerror;

	if (log == NULL || log->log_fd == -1) {
		log_warnx("log file %s not opened", log ? log->log_name : "");
		return (0);
	}

	/* XXX get_string() would sanitize the string, but add a malloc */
	logline = (char *)imsg->data + sizeof(id);

	/* For debug output */
	log_debug("%s", logline);

	if (dprintf(log->log_fd, "%s\n", logline) == -1) {
		if (logger_start() == -1)
			return (-1);
	}

	return (0);
}

int
logger_dispatch_parent(int fd, struct privsep_proc *p, struct imsg *imsg)
{
	switch (imsg->hdr.type) {
	case IMSG_CFG_SERVER:
		config_getserver(httpd_env, imsg);
		break;
	case IMSG_CFG_DONE:
		config_getcfg(httpd_env, imsg);
		break;
	case IMSG_CTL_START:
	case IMSG_CTL_REOPEN:
		logger_start();
		break;
	case IMSG_CTL_RESET:
		config_getreset(httpd_env, imsg);
		break;
	case IMSG_LOG_OPEN:
		return (logger_open_fd(imsg));
	default:
		return (-1);
	}

	return (0);
}

int
logger_dispatch_server(int fd, struct privsep_proc *p, struct imsg *imsg)
{
	switch (imsg->hdr.type) {
	case IMSG_LOG_ACCESS:
	case IMSG_LOG_ERROR:
		logger_log(imsg);
		break;
	default:
		return (-1);
	}

	return (0);
}
