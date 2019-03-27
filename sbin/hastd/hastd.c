/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2010 The FreeBSD Foundation
 * Copyright (c) 2010-2011 Pawel Jakub Dawidek <pawel@dawidek.net>
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

#include <sys/param.h>
#include <sys/linker.h>
#include <sys/module.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <libutil.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <time.h>
#include <unistd.h>

#include <activemap.h>
#include <pjdlog.h>

#include "control.h"
#include "event.h"
#include "hast.h"
#include "hast_proto.h"
#include "hastd.h"
#include "hooks.h"
#include "subr.h"

/* Path to configuration file. */
const char *cfgpath = HAST_CONFIG;
/* Hastd configuration. */
static struct hastd_config *cfg;
/* Was SIGINT or SIGTERM signal received? */
bool sigexit_received = false;
/* Path to pidfile. */
static const char *pidfile;
/* Pidfile handle. */
struct pidfh *pfh;
/* Do we run in foreground? */
static bool foreground;

/* How often check for hooks running for too long. */
#define	REPORT_INTERVAL	5

static void
usage(void)
{

	errx(EX_USAGE, "[-dFh] [-c config] [-P pidfile]");
}

static void
g_gate_load(void)
{

	if (modfind("g_gate") == -1) {
		/* Not present in kernel, try loading it. */
		if (kldload("geom_gate") == -1 || modfind("g_gate") == -1) {
			if (errno != EEXIST) {
				pjdlog_exit(EX_OSERR,
				    "Unable to load geom_gate module");
			}
		}
	}
}

void
descriptors_cleanup(struct hast_resource *res)
{
	struct hast_resource *tres, *tmres;
	struct hastd_listen *lst;

	TAILQ_FOREACH_SAFE(tres, &cfg->hc_resources, hr_next, tmres) {
		if (tres == res) {
			PJDLOG_VERIFY(res->hr_role == HAST_ROLE_SECONDARY ||
			    (res->hr_remotein == NULL &&
			     res->hr_remoteout == NULL));
			continue;
		}
		if (tres->hr_remotein != NULL)
			proto_close(tres->hr_remotein);
		if (tres->hr_remoteout != NULL)
			proto_close(tres->hr_remoteout);
		if (tres->hr_ctrl != NULL)
			proto_close(tres->hr_ctrl);
		if (tres->hr_event != NULL)
			proto_close(tres->hr_event);
		if (tres->hr_conn != NULL)
			proto_close(tres->hr_conn);
		TAILQ_REMOVE(&cfg->hc_resources, tres, hr_next);
		free(tres);
	}
	if (cfg->hc_controlin != NULL)
		proto_close(cfg->hc_controlin);
	proto_close(cfg->hc_controlconn);
	while ((lst = TAILQ_FIRST(&cfg->hc_listen)) != NULL) {
		TAILQ_REMOVE(&cfg->hc_listen, lst, hl_next);
		if (lst->hl_conn != NULL)
			proto_close(lst->hl_conn);
		free(lst);
	}
	(void)pidfile_close(pfh);
	hook_fini();
	pjdlog_fini();
}

static const char *
dtype2str(mode_t mode)
{

	if (S_ISBLK(mode))
		return ("block device");
	else if (S_ISCHR(mode))
		return ("character device");
	else if (S_ISDIR(mode))
		return ("directory");
	else if (S_ISFIFO(mode))
		return ("pipe or FIFO");
	else if (S_ISLNK(mode))
		return ("symbolic link");
	else if (S_ISREG(mode))
		return ("regular file");
	else if (S_ISSOCK(mode))
		return ("socket");
	else if (S_ISWHT(mode))
		return ("whiteout");
	else
		return ("unknown");
}

void
descriptors_assert(const struct hast_resource *res, int pjdlogmode)
{
	char msg[256];
	struct stat sb;
	long maxfd;
	bool isopen;
	mode_t mode;
	int fd;

	/*
	 * At this point descriptor to syslog socket is closed, so if we want
	 * to log assertion message, we have to first store it in 'msg' local
	 * buffer and then open syslog socket and log it.
	 */
	msg[0] = '\0';

	maxfd = sysconf(_SC_OPEN_MAX);
	if (maxfd == -1) {
		pjdlog_init(pjdlogmode);
		pjdlog_prefix_set("[%s] (%s) ", res->hr_name,
		    role2str(res->hr_role));
		pjdlog_errno(LOG_WARNING, "sysconf(_SC_OPEN_MAX) failed");
		pjdlog_fini();
		maxfd = 16384;
	}
	for (fd = 0; fd <= maxfd; fd++) {
		if (fstat(fd, &sb) == 0) {
			isopen = true;
			mode = sb.st_mode;
		} else if (errno == EBADF) {
			isopen = false;
			mode = 0;
		} else {
			(void)snprintf(msg, sizeof(msg),
			    "Unable to fstat descriptor %d: %s", fd,
			    strerror(errno));
			break;
		}
		if (fd == STDIN_FILENO || fd == STDOUT_FILENO ||
		    fd == STDERR_FILENO) {
			if (!isopen) {
				(void)snprintf(msg, sizeof(msg),
				    "Descriptor %d (%s) is closed, but should be open.",
				    fd, (fd == STDIN_FILENO ? "stdin" :
				    (fd == STDOUT_FILENO ? "stdout" : "stderr")));
				break;
			}
		} else if (fd == proto_descriptor(res->hr_event)) {
			if (!isopen) {
				(void)snprintf(msg, sizeof(msg),
				    "Descriptor %d (event) is closed, but should be open.",
				    fd);
				break;
			}
			if (!S_ISSOCK(mode)) {
				(void)snprintf(msg, sizeof(msg),
				    "Descriptor %d (event) is %s, but should be %s.",
				    fd, dtype2str(mode), dtype2str(S_IFSOCK));
				break;
			}
		} else if (fd == proto_descriptor(res->hr_ctrl)) {
			if (!isopen) {
				(void)snprintf(msg, sizeof(msg),
				    "Descriptor %d (ctrl) is closed, but should be open.",
				    fd);
				break;
			}
			if (!S_ISSOCK(mode)) {
				(void)snprintf(msg, sizeof(msg),
				    "Descriptor %d (ctrl) is %s, but should be %s.",
				    fd, dtype2str(mode), dtype2str(S_IFSOCK));
				break;
			}
		} else if (res->hr_role == HAST_ROLE_PRIMARY &&
		    fd == proto_descriptor(res->hr_conn)) {
			if (!isopen) {
				(void)snprintf(msg, sizeof(msg),
				    "Descriptor %d (conn) is closed, but should be open.",
				    fd);
				break;
			}
			if (!S_ISSOCK(mode)) {
				(void)snprintf(msg, sizeof(msg),
				    "Descriptor %d (conn) is %s, but should be %s.",
				    fd, dtype2str(mode), dtype2str(S_IFSOCK));
				break;
			}
		} else if (res->hr_role == HAST_ROLE_SECONDARY &&
		    res->hr_conn != NULL &&
		    fd == proto_descriptor(res->hr_conn)) {
			if (isopen) {
				(void)snprintf(msg, sizeof(msg),
				    "Descriptor %d (conn) is open, but should be closed.",
				    fd);
				break;
			}
		} else if (res->hr_role == HAST_ROLE_SECONDARY &&
		    fd == proto_descriptor(res->hr_remotein)) {
			if (!isopen) {
				(void)snprintf(msg, sizeof(msg),
				    "Descriptor %d (remote in) is closed, but should be open.",
				    fd);
				break;
			}
			if (!S_ISSOCK(mode)) {
				(void)snprintf(msg, sizeof(msg),
				    "Descriptor %d (remote in) is %s, but should be %s.",
				    fd, dtype2str(mode), dtype2str(S_IFSOCK));
				break;
			}
		} else if (res->hr_role == HAST_ROLE_SECONDARY &&
		    fd == proto_descriptor(res->hr_remoteout)) {
			if (!isopen) {
				(void)snprintf(msg, sizeof(msg),
				    "Descriptor %d (remote out) is closed, but should be open.",
				    fd);
				break;
			}
			if (!S_ISSOCK(mode)) {
				(void)snprintf(msg, sizeof(msg),
				    "Descriptor %d (remote out) is %s, but should be %s.",
				    fd, dtype2str(mode), dtype2str(S_IFSOCK));
				break;
			}
		} else {
			if (isopen) {
				(void)snprintf(msg, sizeof(msg),
				    "Descriptor %d is open (%s), but should be closed.",
				    fd, dtype2str(mode));
				break;
			}
		}
	}
	if (msg[0] != '\0') {
		pjdlog_init(pjdlogmode);
		pjdlog_prefix_set("[%s] (%s) ", res->hr_name,
		    role2str(res->hr_role));
		PJDLOG_ABORT("%s", msg);
	}
}

static void
child_exit_log(unsigned int pid, int status)
{

	if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
		pjdlog_debug(1, "Worker process exited gracefully (pid=%u).",
		    pid);
	} else if (WIFSIGNALED(status)) {
		pjdlog_error("Worker process killed (pid=%u, signal=%d).",
		    pid, WTERMSIG(status));
	} else {
		pjdlog_error("Worker process exited ungracefully (pid=%u, exitcode=%d).",
		    pid, WIFEXITED(status) ? WEXITSTATUS(status) : -1);
	}
}

static void
child_exit(void)
{
	struct hast_resource *res;
	int status;
	pid_t pid;

	while ((pid = wait3(&status, WNOHANG, NULL)) > 0) {
		/* Find resource related to the process that just exited. */
		TAILQ_FOREACH(res, &cfg->hc_resources, hr_next) {
			if (pid == res->hr_workerpid)
				break;
		}
		if (res == NULL) {
			/*
			 * This can happen when new connection arrives and we
			 * cancel child responsible for the old one or if this
			 * was hook which we executed.
			 */
			hook_check_one(pid, status);
			continue;
		}
		pjdlog_prefix_set("[%s] (%s) ", res->hr_name,
		    role2str(res->hr_role));
		child_exit_log(pid, status);
		child_cleanup(res);
		if (res->hr_role == HAST_ROLE_PRIMARY) {
			/*
			 * Restart child process if it was killed by signal
			 * or exited because of temporary problem.
			 */
			if (WIFSIGNALED(status) ||
			    (WIFEXITED(status) &&
			     WEXITSTATUS(status) == EX_TEMPFAIL)) {
				sleep(1);
				pjdlog_info("Restarting worker process.");
				hastd_primary(res);
			} else {
				res->hr_role = HAST_ROLE_INIT;
				pjdlog_info("Changing resource role back to %s.",
				    role2str(res->hr_role));
			}
		}
		pjdlog_prefix_set("%s", "");
	}
}

static bool
resource_needs_restart(const struct hast_resource *res0,
    const struct hast_resource *res1)
{

	PJDLOG_ASSERT(strcmp(res0->hr_name, res1->hr_name) == 0);

	if (strcmp(res0->hr_provname, res1->hr_provname) != 0)
		return (true);
	if (strcmp(res0->hr_localpath, res1->hr_localpath) != 0)
		return (true);
	if (res0->hr_role == HAST_ROLE_INIT ||
	    res0->hr_role == HAST_ROLE_SECONDARY) {
		if (strcmp(res0->hr_remoteaddr, res1->hr_remoteaddr) != 0)
			return (true);
		if (strcmp(res0->hr_sourceaddr, res1->hr_sourceaddr) != 0)
			return (true);
		if (res0->hr_replication != res1->hr_replication)
			return (true);
		if (res0->hr_checksum != res1->hr_checksum)
			return (true);
		if (res0->hr_compression != res1->hr_compression)
			return (true);
		if (res0->hr_timeout != res1->hr_timeout)
			return (true);
		if (strcmp(res0->hr_exec, res1->hr_exec) != 0)
			return (true);
		/*
		 * When metaflush has changed we don't really need restart,
		 * but it is just easier this way.
		 */
		if (res0->hr_metaflush != res1->hr_metaflush)
			return (true);
	}
	return (false);
}

static bool
resource_needs_reload(const struct hast_resource *res0,
    const struct hast_resource *res1)
{

	PJDLOG_ASSERT(strcmp(res0->hr_name, res1->hr_name) == 0);
	PJDLOG_ASSERT(strcmp(res0->hr_provname, res1->hr_provname) == 0);
	PJDLOG_ASSERT(strcmp(res0->hr_localpath, res1->hr_localpath) == 0);

	if (res0->hr_role != HAST_ROLE_PRIMARY)
		return (false);

	if (strcmp(res0->hr_remoteaddr, res1->hr_remoteaddr) != 0)
		return (true);
	if (strcmp(res0->hr_sourceaddr, res1->hr_sourceaddr) != 0)
		return (true);
	if (res0->hr_replication != res1->hr_replication)
		return (true);
	if (res0->hr_checksum != res1->hr_checksum)
		return (true);
	if (res0->hr_compression != res1->hr_compression)
		return (true);
	if (res0->hr_timeout != res1->hr_timeout)
		return (true);
	if (strcmp(res0->hr_exec, res1->hr_exec) != 0)
		return (true);
	if (res0->hr_metaflush != res1->hr_metaflush)
		return (true);
	return (false);
}

static void
resource_reload(const struct hast_resource *res)
{
	struct nv *nvin, *nvout;
	int error;

	PJDLOG_ASSERT(res->hr_role == HAST_ROLE_PRIMARY);

	nvout = nv_alloc();
	nv_add_uint8(nvout, CONTROL_RELOAD, "cmd");
	nv_add_string(nvout, res->hr_remoteaddr, "remoteaddr");
	nv_add_string(nvout, res->hr_sourceaddr, "sourceaddr");
	nv_add_int32(nvout, (int32_t)res->hr_replication, "replication");
	nv_add_int32(nvout, (int32_t)res->hr_checksum, "checksum");
	nv_add_int32(nvout, (int32_t)res->hr_compression, "compression");
	nv_add_int32(nvout, (int32_t)res->hr_timeout, "timeout");
	nv_add_string(nvout, res->hr_exec, "exec");
	nv_add_int32(nvout, (int32_t)res->hr_metaflush, "metaflush");
	if (nv_error(nvout) != 0) {
		nv_free(nvout);
		pjdlog_error("Unable to allocate header for reload message.");
		return;
	}
	if (hast_proto_send(res, res->hr_ctrl, nvout, NULL, 0) == -1) {
		pjdlog_errno(LOG_ERR, "Unable to send reload message");
		nv_free(nvout);
		return;
	}
	nv_free(nvout);

	/* Receive response. */
	if (hast_proto_recv_hdr(res->hr_ctrl, &nvin) == -1) {
		pjdlog_errno(LOG_ERR, "Unable to receive reload reply");
		return;
	}
	error = nv_get_int16(nvin, "error");
	nv_free(nvin);
	if (error != 0) {
		pjdlog_common(LOG_ERR, 0, error, "Reload failed");
		return;
	}
}

static void
hastd_reload(void)
{
	struct hastd_config *newcfg;
	struct hast_resource *nres, *cres, *tres;
	struct hastd_listen *nlst, *clst;
	struct pidfh *newpfh;
	unsigned int nlisten;
	uint8_t role;
	pid_t otherpid;

	pjdlog_info("Reloading configuration...");

	newpfh = NULL;

	newcfg = yy_config_parse(cfgpath, false);
	if (newcfg == NULL)
		goto failed;

	/*
	 * Check if control address has changed.
	 */
	if (strcmp(cfg->hc_controladdr, newcfg->hc_controladdr) != 0) {
		if (proto_server(newcfg->hc_controladdr,
		    &newcfg->hc_controlconn) == -1) {
			pjdlog_errno(LOG_ERR,
			    "Unable to listen on control address %s",
			    newcfg->hc_controladdr);
			goto failed;
		}
	}
	/*
	 * Check if any listen address has changed.
	 */
	nlisten = 0;
	TAILQ_FOREACH(nlst, &newcfg->hc_listen, hl_next) {
		TAILQ_FOREACH(clst, &cfg->hc_listen, hl_next) {
			if (strcmp(nlst->hl_addr, clst->hl_addr) == 0)
				break;
		}
		if (clst != NULL && clst->hl_conn != NULL) {
			pjdlog_info("Keep listening on address %s.",
			    nlst->hl_addr);
			nlst->hl_conn = clst->hl_conn;
			nlisten++;
		} else if (proto_server(nlst->hl_addr, &nlst->hl_conn) == 0) {
			pjdlog_info("Listening on new address %s.",
			    nlst->hl_addr);
			nlisten++;
		} else {
			pjdlog_errno(LOG_WARNING,
			    "Unable to listen on address %s", nlst->hl_addr);
		}
	}
	if (nlisten == 0) {
		pjdlog_error("No addresses to listen on.");
		goto failed;
	}
	/*
	 * Check if pidfile's path has changed.
	 */
	if (!foreground && pidfile == NULL &&
	    strcmp(cfg->hc_pidfile, newcfg->hc_pidfile) != 0) {
		newpfh = pidfile_open(newcfg->hc_pidfile, 0600, &otherpid);
		if (newpfh == NULL) {
			if (errno == EEXIST) {
				pjdlog_errno(LOG_WARNING,
				    "Another hastd is already running, pidfile: %s, pid: %jd.",
				    newcfg->hc_pidfile, (intmax_t)otherpid);
			} else {
				pjdlog_errno(LOG_WARNING,
				    "Unable to open or create pidfile %s",
				    newcfg->hc_pidfile);
			}
		} else if (pidfile_write(newpfh) == -1) {
			/* Write PID to a file. */
			pjdlog_errno(LOG_WARNING,
			    "Unable to write PID to file %s",
			    newcfg->hc_pidfile);
		} else {
			pjdlog_debug(1, "PID stored in %s.",
			    newcfg->hc_pidfile);
		}
	}

	/* No failures from now on. */

	/*
	 * Switch to new control socket.
	 */
	if (newcfg->hc_controlconn != NULL) {
		pjdlog_info("Control socket changed from %s to %s.",
		    cfg->hc_controladdr, newcfg->hc_controladdr);
		proto_close(cfg->hc_controlconn);
		cfg->hc_controlconn = newcfg->hc_controlconn;
		newcfg->hc_controlconn = NULL;
		strlcpy(cfg->hc_controladdr, newcfg->hc_controladdr,
		    sizeof(cfg->hc_controladdr));
	}
	/*
	 * Switch to new pidfile.
	 */
	if (newpfh != NULL) {
		pjdlog_info("Pidfile changed from %s to %s.", cfg->hc_pidfile,
		    newcfg->hc_pidfile);
		(void)pidfile_remove(pfh);
		pfh = newpfh;
		(void)strlcpy(cfg->hc_pidfile, newcfg->hc_pidfile,
		    sizeof(cfg->hc_pidfile));
	}
	/*
	 * Switch to new listen addresses. Close all that were removed.
	 */
	while ((clst = TAILQ_FIRST(&cfg->hc_listen)) != NULL) {
		TAILQ_FOREACH(nlst, &newcfg->hc_listen, hl_next) {
			if (strcmp(nlst->hl_addr, clst->hl_addr) == 0)
				break;
		}
		if (nlst == NULL && clst->hl_conn != NULL) {
			proto_close(clst->hl_conn);
			pjdlog_info("No longer listening on address %s.",
			    clst->hl_addr);
		}
		TAILQ_REMOVE(&cfg->hc_listen, clst, hl_next);
		free(clst);
	}
	TAILQ_CONCAT(&cfg->hc_listen, &newcfg->hc_listen, hl_next);

	/*
	 * Stop and remove resources that were removed from the configuration.
	 */
	TAILQ_FOREACH_SAFE(cres, &cfg->hc_resources, hr_next, tres) {
		TAILQ_FOREACH(nres, &newcfg->hc_resources, hr_next) {
			if (strcmp(cres->hr_name, nres->hr_name) == 0)
				break;
		}
		if (nres == NULL) {
			control_set_role(cres, HAST_ROLE_INIT);
			TAILQ_REMOVE(&cfg->hc_resources, cres, hr_next);
			pjdlog_info("Resource %s removed.", cres->hr_name);
			free(cres);
		}
	}
	/*
	 * Move new resources to the current configuration.
	 */
	TAILQ_FOREACH_SAFE(nres, &newcfg->hc_resources, hr_next, tres) {
		TAILQ_FOREACH(cres, &cfg->hc_resources, hr_next) {
			if (strcmp(cres->hr_name, nres->hr_name) == 0)
				break;
		}
		if (cres == NULL) {
			TAILQ_REMOVE(&newcfg->hc_resources, nres, hr_next);
			TAILQ_INSERT_TAIL(&cfg->hc_resources, nres, hr_next);
			pjdlog_info("Resource %s added.", nres->hr_name);
		}
	}
	/*
	 * Deal with modified resources.
	 * Depending on what has changed exactly we might want to perform
	 * different actions.
	 *
	 * We do full resource restart in the following situations:
	 * Resource role is INIT or SECONDARY.
	 * Resource role is PRIMARY and path to local component or provider
	 * name has changed.
	 * In case of PRIMARY, the worker process will be killed and restarted,
	 * which also means removing /dev/hast/<name> provider and
	 * recreating it.
	 *
	 * We do just reload (send SIGHUP to worker process) if we act as
	 * PRIMARY, but only if remote address, source address, replication
	 * mode, timeout, execution path or metaflush has changed.
	 * For those, there is no need to restart worker process.
	 * If PRIMARY receives SIGHUP, it will reconnect if remote address or
	 * source address has changed or it will set new timeout if only timeout
	 * has changed or it will update metaflush if only metaflush has
	 * changed.
	 */
	TAILQ_FOREACH_SAFE(nres, &newcfg->hc_resources, hr_next, tres) {
		TAILQ_FOREACH(cres, &cfg->hc_resources, hr_next) {
			if (strcmp(cres->hr_name, nres->hr_name) == 0)
				break;
		}
		PJDLOG_ASSERT(cres != NULL);
		if (resource_needs_restart(cres, nres)) {
			pjdlog_info("Resource %s configuration was modified, restarting it.",
			    cres->hr_name);
			role = cres->hr_role;
			control_set_role(cres, HAST_ROLE_INIT);
			TAILQ_REMOVE(&cfg->hc_resources, cres, hr_next);
			free(cres);
			TAILQ_REMOVE(&newcfg->hc_resources, nres, hr_next);
			TAILQ_INSERT_TAIL(&cfg->hc_resources, nres, hr_next);
			control_set_role(nres, role);
		} else if (resource_needs_reload(cres, nres)) {
			pjdlog_info("Resource %s configuration was modified, reloading it.",
			    cres->hr_name);
			strlcpy(cres->hr_remoteaddr, nres->hr_remoteaddr,
			    sizeof(cres->hr_remoteaddr));
			strlcpy(cres->hr_sourceaddr, nres->hr_sourceaddr,
			    sizeof(cres->hr_sourceaddr));
			cres->hr_replication = nres->hr_replication;
			cres->hr_checksum = nres->hr_checksum;
			cres->hr_compression = nres->hr_compression;
			cres->hr_timeout = nres->hr_timeout;
			strlcpy(cres->hr_exec, nres->hr_exec,
			    sizeof(cres->hr_exec));
			cres->hr_metaflush = nres->hr_metaflush;
			if (cres->hr_workerpid != 0)
				resource_reload(cres);
		}
	}

	yy_config_free(newcfg);
	pjdlog_info("Configuration reloaded successfully.");
	return;
failed:
	if (newcfg != NULL) {
		if (newcfg->hc_controlconn != NULL)
			proto_close(newcfg->hc_controlconn);
		while ((nlst = TAILQ_FIRST(&newcfg->hc_listen)) != NULL) {
			if (nlst->hl_conn != NULL) {
				TAILQ_FOREACH(clst, &cfg->hc_listen, hl_next) {
					if (strcmp(nlst->hl_addr,
					    clst->hl_addr) == 0) {
						break;
					}
				}
				if (clst == NULL || clst->hl_conn == NULL)
					proto_close(nlst->hl_conn);
			}
			TAILQ_REMOVE(&newcfg->hc_listen, nlst, hl_next);
			free(nlst);
		}
		yy_config_free(newcfg);
	}
	if (newpfh != NULL)
		(void)pidfile_remove(newpfh);
	pjdlog_warning("Configuration not reloaded.");
}

static void
terminate_workers(void)
{
	struct hast_resource *res;

	pjdlog_info("Termination signal received, exiting.");
	TAILQ_FOREACH(res, &cfg->hc_resources, hr_next) {
		if (res->hr_workerpid == 0)
			continue;
		pjdlog_info("Terminating worker process (resource=%s, role=%s, pid=%u).",
		    res->hr_name, role2str(res->hr_role), res->hr_workerpid);
		if (kill(res->hr_workerpid, SIGTERM) == 0)
			continue;
		pjdlog_errno(LOG_WARNING,
		    "Unable to send signal to worker process (resource=%s, role=%s, pid=%u).",
		    res->hr_name, role2str(res->hr_role), res->hr_workerpid);
	}
}

static void
listen_accept(struct hastd_listen *lst)
{
	struct hast_resource *res;
	struct proto_conn *conn;
	struct nv *nvin, *nvout, *nverr;
	const char *resname;
	const unsigned char *token;
	char laddr[256], raddr[256];
	uint8_t version;
	size_t size;
	pid_t pid;
	int status;

	proto_local_address(lst->hl_conn, laddr, sizeof(laddr));
	pjdlog_debug(1, "Accepting connection to %s.", laddr);

	if (proto_accept(lst->hl_conn, &conn) == -1) {
		pjdlog_errno(LOG_ERR, "Unable to accept connection %s", laddr);
		return;
	}

	proto_local_address(conn, laddr, sizeof(laddr));
	proto_remote_address(conn, raddr, sizeof(raddr));
	pjdlog_info("Connection from %s to %s.", raddr, laddr);

	/* Error in setting timeout is not critical, but why should it fail? */
	if (proto_timeout(conn, HAST_TIMEOUT) == -1)
		pjdlog_errno(LOG_WARNING, "Unable to set connection timeout");

	nvin = nvout = nverr = NULL;

	/*
	 * Before receiving any data see if remote host have access to any
	 * resource.
	 */
	TAILQ_FOREACH(res, &cfg->hc_resources, hr_next) {
		if (proto_address_match(conn, res->hr_remoteaddr))
			break;
	}
	if (res == NULL) {
		pjdlog_error("Client %s isn't known.", raddr);
		goto close;
	}
	/* Ok, remote host can access at least one resource. */

	if (hast_proto_recv_hdr(conn, &nvin) == -1) {
		pjdlog_errno(LOG_ERR, "Unable to receive header from %s",
		    raddr);
		goto close;
	}

	resname = nv_get_string(nvin, "resource");
	if (resname == NULL) {
		pjdlog_error("No 'resource' field in the header received from %s.",
		    raddr);
		goto close;
	}
	pjdlog_debug(2, "%s: resource=%s", raddr, resname);
	version = nv_get_uint8(nvin, "version");
	pjdlog_debug(2, "%s: version=%hhu", raddr, version);
	if (version == 0) {
		/*
		 * If no version is sent, it means this is protocol version 1.
		 */
		version = 1;
	}
	token = nv_get_uint8_array(nvin, &size, "token");
	/*
	 * NULL token means that this is first connection.
	 */
	if (token != NULL && size != sizeof(res->hr_token)) {
		pjdlog_error("Received token of invalid size from %s (expected %zu, got %zu).",
		    raddr, sizeof(res->hr_token), size);
		goto close;
	}

	/*
	 * From now on we want to send errors to the remote node.
	 */
	nverr = nv_alloc();

	/* Find resource related to this connection. */
	TAILQ_FOREACH(res, &cfg->hc_resources, hr_next) {
		if (strcmp(resname, res->hr_name) == 0)
			break;
	}
	/* Have we found the resource? */
	if (res == NULL) {
		pjdlog_error("No resource '%s' as requested by %s.",
		    resname, raddr);
		nv_add_stringf(nverr, "errmsg", "Resource not configured.");
		goto fail;
	}

	/* Now that we know resource name setup log prefix. */
	pjdlog_prefix_set("[%s] (%s) ", res->hr_name, role2str(res->hr_role));

	/* Does the remote host have access to this resource? */
	if (!proto_address_match(conn, res->hr_remoteaddr)) {
		pjdlog_error("Client %s has no access to the resource.", raddr);
		nv_add_stringf(nverr, "errmsg", "No access to the resource.");
		goto fail;
	}
	/* Is the resource marked as secondary? */
	if (res->hr_role != HAST_ROLE_SECONDARY) {
		pjdlog_warning("We act as %s for the resource and not as %s as requested by %s.",
		    role2str(res->hr_role), role2str(HAST_ROLE_SECONDARY),
		    raddr);
		nv_add_stringf(nverr, "errmsg",
		    "Remote node acts as %s for the resource and not as %s.",
		    role2str(res->hr_role), role2str(HAST_ROLE_SECONDARY));
		if (res->hr_role == HAST_ROLE_PRIMARY) {
			/*
			 * If we act as primary request the other side to wait
			 * for us a bit, as we might be finishing cleanups.
			 */
			nv_add_uint8(nverr, 1, "wait");
		}
		goto fail;
	}
	/* Does token (if exists) match? */
	if (token != NULL && memcmp(token, res->hr_token,
	    sizeof(res->hr_token)) != 0) {
		pjdlog_error("Token received from %s doesn't match.", raddr);
		nv_add_stringf(nverr, "errmsg", "Token doesn't match.");
		goto fail;
	}
	/*
	 * If there is no token, but we have half-open connection
	 * (only remotein) or full connection (worker process is running)
	 * we have to cancel those and accept the new connection.
	 */
	if (token == NULL) {
		PJDLOG_ASSERT(res->hr_remoteout == NULL);
		pjdlog_debug(1, "Initial connection from %s.", raddr);
		if (res->hr_workerpid != 0) {
			PJDLOG_ASSERT(res->hr_remotein == NULL);
			pjdlog_debug(1,
			    "Worker process exists (pid=%u), stopping it.",
			    (unsigned int)res->hr_workerpid);
			/* Stop child process. */
			if (kill(res->hr_workerpid, SIGINT) == -1) {
				pjdlog_errno(LOG_ERR,
				    "Unable to stop worker process (pid=%u)",
				    (unsigned int)res->hr_workerpid);
				/*
				 * Other than logging the problem we
				 * ignore it - nothing smart to do.
				 */
			}
			/* Wait for it to exit. */
			else if ((pid = waitpid(res->hr_workerpid,
			    &status, 0)) != res->hr_workerpid) {
				/* We can only log the problem. */
				pjdlog_errno(LOG_ERR,
				    "Waiting for worker process (pid=%u) failed",
				    (unsigned int)res->hr_workerpid);
			} else {
				child_exit_log(res->hr_workerpid, status);
			}
			child_cleanup(res);
		} else if (res->hr_remotein != NULL) {
			char oaddr[256];

			proto_remote_address(res->hr_remotein, oaddr,
			    sizeof(oaddr));
			pjdlog_debug(1,
			    "Canceling half-open connection from %s on connection from %s.",
			    oaddr, raddr);
			proto_close(res->hr_remotein);
			res->hr_remotein = NULL;
		}
	}

	/*
	 * Checks and cleanups are done.
	 */

	if (token == NULL) {
		if (version > HAST_PROTO_VERSION) {
			pjdlog_info("Remote protocol version %hhu is not supported, falling back to version %hhu.",
			    version, (unsigned char)HAST_PROTO_VERSION);
			version = HAST_PROTO_VERSION;
		}
		pjdlog_debug(1, "Negotiated protocol version %hhu.", version);
		res->hr_version = version;
		arc4random_buf(res->hr_token, sizeof(res->hr_token));
		nvout = nv_alloc();
		nv_add_uint8(nvout, version, "version");
		nv_add_uint8_array(nvout, res->hr_token,
		    sizeof(res->hr_token), "token");
		if (nv_error(nvout) != 0) {
			pjdlog_common(LOG_ERR, 0, nv_error(nvout),
			    "Unable to prepare return header for %s", raddr);
			nv_add_stringf(nverr, "errmsg",
			    "Remote node was unable to prepare return header: %s.",
			    strerror(nv_error(nvout)));
			goto fail;
		}
		if (hast_proto_send(res, conn, nvout, NULL, 0) == -1) {
			int error = errno;

			pjdlog_errno(LOG_ERR, "Unable to send response to %s",
			    raddr);
			nv_add_stringf(nverr, "errmsg",
			    "Remote node was unable to send response: %s.",
			    strerror(error));
			goto fail;
		}
		res->hr_remotein = conn;
		pjdlog_debug(1, "Incoming connection from %s configured.",
		    raddr);
	} else {
		res->hr_remoteout = conn;
		pjdlog_debug(1, "Outgoing connection to %s configured.", raddr);
		hastd_secondary(res, nvin);
	}
	nv_free(nvin);
	nv_free(nvout);
	nv_free(nverr);
	pjdlog_prefix_set("%s", "");
	return;
fail:
	if (nv_error(nverr) != 0) {
		pjdlog_common(LOG_ERR, 0, nv_error(nverr),
		    "Unable to prepare error header for %s", raddr);
		goto close;
	}
	if (hast_proto_send(NULL, conn, nverr, NULL, 0) == -1) {
		pjdlog_errno(LOG_ERR, "Unable to send error to %s", raddr);
		goto close;
	}
close:
	if (nvin != NULL)
		nv_free(nvin);
	if (nvout != NULL)
		nv_free(nvout);
	if (nverr != NULL)
		nv_free(nverr);
	proto_close(conn);
	pjdlog_prefix_set("%s", "");
}

static void
connection_migrate(struct hast_resource *res)
{
	struct proto_conn *conn;
	int16_t val = 0;

	pjdlog_prefix_set("[%s] (%s) ", res->hr_name, role2str(res->hr_role));

	PJDLOG_ASSERT(res->hr_role == HAST_ROLE_PRIMARY);

	if (proto_recv(res->hr_conn, &val, sizeof(val)) == -1) {
		pjdlog_errno(LOG_WARNING,
		    "Unable to receive connection command");
		return;
	}
	if (proto_client(res->hr_sourceaddr[0] != '\0' ? res->hr_sourceaddr : NULL,
	    res->hr_remoteaddr, &conn) == -1) {
		val = errno;
		pjdlog_errno(LOG_WARNING,
		    "Unable to create outgoing connection to %s",
		    res->hr_remoteaddr);
		goto out;
	}
	if (proto_connect(conn, -1) == -1) {
		val = errno;
		pjdlog_errno(LOG_WARNING, "Unable to connect to %s",
		    res->hr_remoteaddr);
		proto_close(conn);
		goto out;
	}
	val = 0;
out:
	if (proto_send(res->hr_conn, &val, sizeof(val)) == -1) {
		pjdlog_errno(LOG_WARNING,
		    "Unable to send reply to connection request");
	}
	if (val == 0 && proto_connection_send(res->hr_conn, conn) == -1)
		pjdlog_errno(LOG_WARNING, "Unable to send connection");

	pjdlog_prefix_set("%s", "");
}

static void
check_signals(void)
{
	struct timespec sigtimeout;
	sigset_t mask;
	int signo;

	sigtimeout.tv_sec = 0;
	sigtimeout.tv_nsec = 0;

	PJDLOG_VERIFY(sigemptyset(&mask) == 0);
	PJDLOG_VERIFY(sigaddset(&mask, SIGHUP) == 0);
	PJDLOG_VERIFY(sigaddset(&mask, SIGINT) == 0);
	PJDLOG_VERIFY(sigaddset(&mask, SIGTERM) == 0);
	PJDLOG_VERIFY(sigaddset(&mask, SIGCHLD) == 0);

	while ((signo = sigtimedwait(&mask, NULL, &sigtimeout)) != -1) {
		switch (signo) {
		case SIGINT:
		case SIGTERM:
			sigexit_received = true;
			terminate_workers();
			proto_close(cfg->hc_controlconn);
			exit(EX_OK);
			break;
		case SIGCHLD:
			child_exit();
			break;
		case SIGHUP:
			hastd_reload();
			break;
		default:
			PJDLOG_ABORT("Unexpected signal (%d).", signo);
		}
	}
}

static void
main_loop(void)
{
	struct hast_resource *res;
	struct hastd_listen *lst;
	struct timeval seltimeout;
	int fd, maxfd, ret;
	time_t lastcheck, now;
	fd_set rfds;

	lastcheck = time(NULL);
	seltimeout.tv_sec = REPORT_INTERVAL;
	seltimeout.tv_usec = 0;

	for (;;) {
		check_signals();

		/* Setup descriptors for select(2). */
		FD_ZERO(&rfds);
		maxfd = fd = proto_descriptor(cfg->hc_controlconn);
		PJDLOG_ASSERT(fd >= 0);
		FD_SET(fd, &rfds);
		TAILQ_FOREACH(lst, &cfg->hc_listen, hl_next) {
			if (lst->hl_conn == NULL)
				continue;
			fd = proto_descriptor(lst->hl_conn);
			PJDLOG_ASSERT(fd >= 0);
			FD_SET(fd, &rfds);
			maxfd = MAX(fd, maxfd);
		}
		TAILQ_FOREACH(res, &cfg->hc_resources, hr_next) {
			if (res->hr_event == NULL)
				continue;
			fd = proto_descriptor(res->hr_event);
			PJDLOG_ASSERT(fd >= 0);
			FD_SET(fd, &rfds);
			maxfd = MAX(fd, maxfd);
			if (res->hr_role == HAST_ROLE_PRIMARY) {
				/* Only primary workers asks for connections. */
				PJDLOG_ASSERT(res->hr_conn != NULL);
				fd = proto_descriptor(res->hr_conn);
				PJDLOG_ASSERT(fd >= 0);
				FD_SET(fd, &rfds);
				maxfd = MAX(fd, maxfd);
			} else {
				PJDLOG_ASSERT(res->hr_conn == NULL);
			}
		}

		PJDLOG_ASSERT(maxfd + 1 <= (int)FD_SETSIZE);
		ret = select(maxfd + 1, &rfds, NULL, NULL, &seltimeout);
		now = time(NULL);
		if (lastcheck + REPORT_INTERVAL <= now) {
			hook_check();
			lastcheck = now;
		}
		if (ret == 0) {
			/*
			 * select(2) timed out, so there should be no
			 * descriptors to check.
			 */
			continue;
		} else if (ret == -1) {
			if (errno == EINTR)
				continue;
			KEEP_ERRNO((void)pidfile_remove(pfh));
			pjdlog_exit(EX_OSERR, "select() failed");
		}

		/*
		 * Check for signals before we do anything to update our
		 * info about terminated workers in the meantime.
		 */
		check_signals();

		if (FD_ISSET(proto_descriptor(cfg->hc_controlconn), &rfds))
			control_handle(cfg);
		TAILQ_FOREACH(lst, &cfg->hc_listen, hl_next) {
			if (lst->hl_conn == NULL)
				continue;
			if (FD_ISSET(proto_descriptor(lst->hl_conn), &rfds))
				listen_accept(lst);
		}
		TAILQ_FOREACH(res, &cfg->hc_resources, hr_next) {
			if (res->hr_event == NULL)
				continue;
			if (FD_ISSET(proto_descriptor(res->hr_event), &rfds)) {
				if (event_recv(res) == 0)
					continue;
				/* The worker process exited? */
				proto_close(res->hr_event);
				res->hr_event = NULL;
				if (res->hr_conn != NULL) {
					proto_close(res->hr_conn);
					res->hr_conn = NULL;
				}
				continue;
			}
			if (res->hr_role == HAST_ROLE_PRIMARY) {
				PJDLOG_ASSERT(res->hr_conn != NULL);
				if (FD_ISSET(proto_descriptor(res->hr_conn),
				    &rfds)) {
					connection_migrate(res);
				}
			} else {
				PJDLOG_ASSERT(res->hr_conn == NULL);
			}
		}
	}
}

static void
dummy_sighandler(int sig __unused)
{
	/* Nothing to do. */
}

int
main(int argc, char *argv[])
{
	struct hastd_listen *lst;
	pid_t otherpid;
	int debuglevel;
	sigset_t mask;

	foreground = false;
	debuglevel = 0;

	for (;;) {
		int ch;

		ch = getopt(argc, argv, "c:dFhP:");
		if (ch == -1)
			break;
		switch (ch) {
		case 'c':
			cfgpath = optarg;
			break;
		case 'd':
			debuglevel++;
			break;
		case 'F':
			foreground = true;
			break;
		case 'P':
			pidfile = optarg;
			break;
		case 'h':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	pjdlog_init(PJDLOG_MODE_STD);
	pjdlog_debug_set(debuglevel);

	g_gate_load();

	/*
	 * When path to the configuration file is relative, obtain full path,
	 * so we can always find the file, even after daemonizing and changing
	 * working directory to /.
	 */
	if (cfgpath[0] != '/') {
		const char *newcfgpath;

		newcfgpath = realpath(cfgpath, NULL);
		if (newcfgpath == NULL) {
			pjdlog_exit(EX_CONFIG,
			    "Unable to obtain full path of %s", cfgpath);
		}
		cfgpath = newcfgpath;
	}

	cfg = yy_config_parse(cfgpath, true);
	PJDLOG_ASSERT(cfg != NULL);

	if (pidfile != NULL) {
		if (strlcpy(cfg->hc_pidfile, pidfile,
		    sizeof(cfg->hc_pidfile)) >= sizeof(cfg->hc_pidfile)) {
			pjdlog_exitx(EX_CONFIG, "Pidfile path is too long.");
		}
	}

	if (pidfile != NULL || !foreground) {
		pfh = pidfile_open(cfg->hc_pidfile, 0600, &otherpid);
		if (pfh == NULL) {
			if (errno == EEXIST) {
				pjdlog_exitx(EX_TEMPFAIL,
				    "Another hastd is already running, pidfile: %s, pid: %jd.",
				    cfg->hc_pidfile, (intmax_t)otherpid);
			}
			/*
			 * If we cannot create pidfile for other reasons,
			 * only warn.
			 */
			pjdlog_errno(LOG_WARNING,
			    "Unable to open or create pidfile %s",
			    cfg->hc_pidfile);
		}
	}

	/*
	 * Restore default actions for interesting signals in case parent
	 * process (like init(8)) decided to ignore some of them (like SIGHUP).
	 */
	PJDLOG_VERIFY(signal(SIGHUP, SIG_DFL) != SIG_ERR);
	PJDLOG_VERIFY(signal(SIGINT, SIG_DFL) != SIG_ERR);
	PJDLOG_VERIFY(signal(SIGTERM, SIG_DFL) != SIG_ERR);
	/*
	 * Because SIGCHLD is ignored by default, setup dummy handler for it,
	 * so we can mask it.
	 */
	PJDLOG_VERIFY(signal(SIGCHLD, dummy_sighandler) != SIG_ERR);

	PJDLOG_VERIFY(sigemptyset(&mask) == 0);
	PJDLOG_VERIFY(sigaddset(&mask, SIGHUP) == 0);
	PJDLOG_VERIFY(sigaddset(&mask, SIGINT) == 0);
	PJDLOG_VERIFY(sigaddset(&mask, SIGTERM) == 0);
	PJDLOG_VERIFY(sigaddset(&mask, SIGCHLD) == 0);
	PJDLOG_VERIFY(sigprocmask(SIG_SETMASK, &mask, NULL) == 0);

	/* Listen on control address. */
	if (proto_server(cfg->hc_controladdr, &cfg->hc_controlconn) == -1) {
		KEEP_ERRNO((void)pidfile_remove(pfh));
		pjdlog_exit(EX_OSERR, "Unable to listen on control address %s",
		    cfg->hc_controladdr);
	}
	/* Listen for remote connections. */
	TAILQ_FOREACH(lst, &cfg->hc_listen, hl_next) {
		if (proto_server(lst->hl_addr, &lst->hl_conn) == -1) {
			KEEP_ERRNO((void)pidfile_remove(pfh));
			pjdlog_exit(EX_OSERR, "Unable to listen on address %s",
			    lst->hl_addr);
		}
	}

	if (!foreground) {
		if (daemon(0, 0) == -1) {
			KEEP_ERRNO((void)pidfile_remove(pfh));
			pjdlog_exit(EX_OSERR, "Unable to daemonize");
		}

		/* Start logging to syslog. */
		pjdlog_mode_set(PJDLOG_MODE_SYSLOG);
	}
	if (pidfile != NULL || !foreground) {
		/* Write PID to a file. */
		if (pidfile_write(pfh) == -1) {
			pjdlog_errno(LOG_WARNING,
			    "Unable to write PID to a file %s",
			    cfg->hc_pidfile);
		} else {
			pjdlog_debug(1, "PID stored in %s.", cfg->hc_pidfile);
		}
	}

	pjdlog_info("Started successfully, running protocol version %d.",
	    HAST_PROTO_VERSION);

	pjdlog_debug(1, "Listening on control address %s.",
	    cfg->hc_controladdr);
	TAILQ_FOREACH(lst, &cfg->hc_listen, hl_next)
		pjdlog_info("Listening on address %s.", lst->hl_addr);

	hook_init();

	main_loop();

	exit(0);
}
