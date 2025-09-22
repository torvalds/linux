/*	$OpenBSD: proc.c,v 1.52 2024/11/21 13:38:45 claudio Exp $	*/

/*
 * Copyright (c) 2010 - 2016 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2008 Pierre-Yves Ritschard <pyr@openbsd.org>
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
#include <sys/socket.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <paths.h>
#include <pwd.h>
#include <event.h>
#include <imsg.h>

#include "httpd.h"

void	 proc_exec(struct privsep *, struct privsep_proc *, unsigned int, int,
	    char **);
void	 proc_setup(struct privsep *, struct privsep_proc *, unsigned int);
void	 proc_open(struct privsep *, int, int);
void	 proc_accept(struct privsep *, int, enum privsep_procid,
	    unsigned int);
void	 proc_close(struct privsep *);
void	 proc_shutdown(struct privsep_proc *);
void	 proc_sig_handler(int, short, void *);
void	 proc_range(struct privsep *, enum privsep_procid, int *, int *);
int	 proc_dispatch_null(int, struct privsep_proc *, struct imsg *);

enum privsep_procid
proc_getid(struct privsep_proc *procs, unsigned int nproc,
    const char *proc_name)
{
	struct privsep_proc	*p;
	unsigned int		 proc;

	for (proc = 0; proc < nproc; proc++) {
		p = &procs[proc];
		if (strcmp(p->p_title, proc_name))
			continue;

		return (p->p_id);
	}

	return (PROC_MAX);
}

void
proc_exec(struct privsep *ps, struct privsep_proc *procs, unsigned int nproc,
    int argc, char **argv)
{
	unsigned int		 proc, nargc, i, proc_i;
	char			**nargv;
	struct privsep_proc	*p;
	char			 num[32];
	int			 fd;

	/* Prepare the new process argv. */
	nargv = calloc(argc + 5, sizeof(char *));
	if (nargv == NULL)
		fatal("%s: calloc", __func__);

	/* Copy call argument first. */
	nargc = 0;
	nargv[nargc++] = argv[0];

	/* Set process name argument and save the position. */
	nargv[nargc++] = "-P";
	proc_i = nargc;
	nargc++;

	/* Point process instance arg to stack and copy the original args. */
	nargv[nargc++] = "-I";
	nargv[nargc++] = num;
	for (i = 1; i < (unsigned int) argc; i++)
		nargv[nargc++] = argv[i];

	nargv[nargc] = NULL;

	for (proc = 0; proc < nproc; proc++) {
		p = &procs[proc];

		/* Update args with process title. */
		nargv[proc_i] = (char *)(uintptr_t)p->p_title;

		/* Fire children processes. */
		for (i = 0; i < ps->ps_instances[p->p_id]; i++) {
			/* Update the process instance number. */
			snprintf(num, sizeof(num), "%u", i);

			fd = ps->ps_pipes[p->p_id][i].pp_pipes[PROC_PARENT][0];
			ps->ps_pipes[p->p_id][i].pp_pipes[PROC_PARENT][0] = -1;

			switch (fork()) {
			case -1:
				fatal("%s: fork", __func__);
				break;
			case 0:
				/* Prepare parent socket. */
				if (fd != PROC_PARENT_SOCK_FILENO) {
					if (dup2(fd, PROC_PARENT_SOCK_FILENO)
					    == -1)
						fatal("dup2");
				} else if (fcntl(fd, F_SETFD, 0) == -1)
					fatal("fcntl");

				execvp(argv[0], nargv);
				fatal("%s: execvp", __func__);
				break;
			default:
				/* Close child end. */
				close(fd);
				break;
			}
		}
	}
	free(nargv);
}

void
proc_connect(struct privsep *ps)
{
	struct imsgev		*iev;
	unsigned int		 src, dst, inst;

	/* Don't distribute any sockets if we are not really going to run. */
	if (ps->ps_noaction)
		return;

	for (dst = 0; dst < PROC_MAX; dst++) {
		/* We don't communicate with ourselves. */
		if (dst == PROC_PARENT)
			continue;

		for (inst = 0; inst < ps->ps_instances[dst]; inst++) {
			iev = &ps->ps_ievs[dst][inst];
			if (imsgbuf_init(&iev->ibuf,
			    ps->ps_pp->pp_pipes[dst][inst]) == -1)
				fatal(NULL);
			imsgbuf_allow_fdpass(&iev->ibuf);
			event_set(&iev->ev, iev->ibuf.fd, iev->events,
			    iev->handler, iev->data);
			event_add(&iev->ev, NULL);
		}
	}

	/* Distribute the socketpair()s for everyone. */
	for (src = 0; src < PROC_MAX; src++)
		for (dst = src; dst < PROC_MAX; dst++) {
			/* Parent already distributed its fds. */
			if (src == PROC_PARENT || dst == PROC_PARENT)
				continue;

			proc_open(ps, src, dst);
		}
}

void
proc_init(struct privsep *ps, struct privsep_proc *procs, unsigned int nproc,
    int debug, int argc, char **argv, enum privsep_procid proc_id)
{
	struct privsep_proc	*p = NULL;
	struct privsep_pipes	*pa, *pb;
	unsigned int		 proc;
	unsigned int		 dst;
	int			 fds[2];

	/* Don't initiate anything if we are not really going to run. */
	if (ps->ps_noaction)
		return;

	if (proc_id == PROC_PARENT) {
		privsep_process = PROC_PARENT;
		proc_setup(ps, procs, nproc);

		if (!debug && daemon(1, 0) == -1)
			fatal("failed to daemonize");

		/*
		 * Create the children sockets so we can use them
		 * to distribute the rest of the socketpair()s using
		 * proc_connect() later.
		 */
		for (dst = 0; dst < PROC_MAX; dst++) {
			/* Don't create socket for ourselves. */
			if (dst == PROC_PARENT)
				continue;

			for (proc = 0; proc < ps->ps_instances[dst]; proc++) {
				pa = &ps->ps_pipes[PROC_PARENT][0];
				pb = &ps->ps_pipes[dst][proc];
				if (socketpair(AF_UNIX,
				    SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
				    PF_UNSPEC, fds) == -1)
					fatal("%s: socketpair", __func__);

				pa->pp_pipes[dst][proc] = fds[0];
				pb->pp_pipes[PROC_PARENT][0] = fds[1];
			}
		}

		/* Engage! */
		proc_exec(ps, procs, nproc, argc, argv);
		return;
	}

	/* Initialize a child */
	for (proc = 0; proc < nproc; proc++) {
		if (procs[proc].p_id != proc_id)
			continue;
		p = &procs[proc];
		break;
	}
	if (p == NULL || p->p_init == NULL)
		fatalx("%s: process %d missing process initialization",
		    __func__, proc_id);

	p->p_init(ps, p);

	fatalx("failed to initiate child process");
}

void
proc_accept(struct privsep *ps, int fd, enum privsep_procid dst,
    unsigned int n)
{
	struct privsep_pipes	*pp = ps->ps_pp;
	struct imsgev		*iev;

	if (ps->ps_ievs[dst] == NULL) {
#if DEBUG > 1
		log_debug("%s: %s src %d %d to dst %d %d not connected",
		    __func__, ps->ps_title[privsep_process],
		    privsep_process, ps->ps_instance + 1,
		    dst, n + 1);
#endif
		close(fd);
		return;
	}

	if (pp->pp_pipes[dst][n] != -1) {
		log_warnx("%s: duplicated descriptor", __func__);
		close(fd);
		return;
	} else
		pp->pp_pipes[dst][n] = fd;

	iev = &ps->ps_ievs[dst][n];
	if (imsgbuf_init(&iev->ibuf, fd) == -1)
		fatal(NULL);
	imsgbuf_allow_fdpass(&iev->ibuf);
	event_set(&iev->ev, iev->ibuf.fd, iev->events, iev->handler, iev->data);
	event_add(&iev->ev, NULL);
}

void
proc_setup(struct privsep *ps, struct privsep_proc *procs, unsigned int nproc)
{
	unsigned int		 i, j, src, dst, id;
	struct privsep_pipes	*pp;

	/* Initialize parent title, ps_instances and procs. */
	ps->ps_title[PROC_PARENT] = "parent";

	for (src = 0; src < PROC_MAX; src++)
		/* Default to 1 process instance */
		if (ps->ps_instances[src] < 1)
			ps->ps_instances[src] = 1;

	for (src = 0; src < nproc; src++) {
		procs[src].p_ps = ps;
		if (procs[src].p_cb == NULL)
			procs[src].p_cb = proc_dispatch_null;

		id = procs[src].p_id;
		ps->ps_title[id] = procs[src].p_title;
		if ((ps->ps_ievs[id] = calloc(ps->ps_instances[id],
		    sizeof(struct imsgev))) == NULL)
			fatal("%s: calloc", __func__);

		/* With this set up, we are ready to call imsgbuf_init(). */
		for (i = 0; i < ps->ps_instances[id]; i++) {
			ps->ps_ievs[id][i].handler = proc_dispatch;
			ps->ps_ievs[id][i].events = EV_READ;
			ps->ps_ievs[id][i].proc = &procs[src];
			ps->ps_ievs[id][i].data = &ps->ps_ievs[id][i];
		}
	}

	/*
	 * Allocate pipes for all process instances (incl. parent)
	 *
	 * - ps->ps_pipes: N:M mapping
	 * N source processes connected to M destination processes:
	 * [src][instances][dst][instances], for example
	 * [PROC_RELAY][3][PROC_CA][3]
	 *
	 * - ps->ps_pp: per-process 1:M part of ps->ps_pipes
	 * Each process instance has a destination array of socketpair fds:
	 * [dst][instances], for example
	 * [PROC_PARENT][0]
	 */
	for (src = 0; src < PROC_MAX; src++) {
		/* Allocate destination array for each process */
		if ((ps->ps_pipes[src] = calloc(ps->ps_instances[src],
		    sizeof(struct privsep_pipes))) == NULL)
			fatal("%s: calloc", __func__);

		for (i = 0; i < ps->ps_instances[src]; i++) {
			pp = &ps->ps_pipes[src][i];

			for (dst = 0; dst < PROC_MAX; dst++) {
				/* Allocate maximum fd integers */
				if ((pp->pp_pipes[dst] =
				    calloc(ps->ps_instances[dst],
				    sizeof(int))) == NULL)
					fatal("%s: calloc", __func__);

				/* Mark fd as unused */
				for (j = 0; j < ps->ps_instances[dst]; j++)
					pp->pp_pipes[dst][j] = -1;
			}
		}
	}

	ps->ps_pp = &ps->ps_pipes[privsep_process][ps->ps_instance];
}

void
proc_kill(struct privsep *ps)
{
	char		*cause;
	pid_t		 pid;
	int		 len, status;

	if (privsep_process != PROC_PARENT)
		return;

	proc_close(ps);

	do {
		pid = waitpid(WAIT_ANY, &status, 0);
		if (pid <= 0)
			continue;

		if (WIFSIGNALED(status)) {
			len = asprintf(&cause, "terminated; signal %d",
			    WTERMSIG(status));
		} else if (WIFEXITED(status)) {
			if (WEXITSTATUS(status) != 0)
				len = asprintf(&cause, "exited abnormally");
			else
				len = 0;
		} else
			len = -1;

		if (len == 0) {
			/* child exited OK, don't print a warning message */
		} else if (len != -1) {
			log_warnx("lost child: pid %u %s", pid, cause);
			free(cause);
		} else
			log_warnx("lost child: pid %u", pid);
	} while (pid != -1 || errno == EINTR);
}

void
proc_open(struct privsep *ps, int src, int dst)
{
	struct privsep_pipes	*pa, *pb;
	struct privsep_fd	 pf;
	int			 fds[2];
	unsigned int		 i, j;

	/* Exchange pipes between process. */
	for (i = 0; i < ps->ps_instances[src]; i++) {
		for (j = 0; j < ps->ps_instances[dst]; j++) {
			/* Don't create sockets for ourself. */
			if (src == dst && i == j)
				continue;

			/* Servers don't talk to each other. */
			if (src == PROC_SERVER && dst == PROC_SERVER)
				continue;

			pa = &ps->ps_pipes[src][i];
			pb = &ps->ps_pipes[dst][j];
			if (socketpair(AF_UNIX,
			    SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
			    PF_UNSPEC, fds) == -1)
				fatal("%s: socketpair", __func__);

			pa->pp_pipes[dst][j] = fds[0];
			pb->pp_pipes[src][i] = fds[1];

			pf.pf_procid = src;
			pf.pf_instance = i;
			if (proc_compose_imsg(ps, dst, j, IMSG_CTL_PROCFD,
			    -1, pb->pp_pipes[src][i], &pf, sizeof(pf)) == -1)
				fatal("%s: proc_compose_imsg", __func__);

			pf.pf_procid = dst;
			pf.pf_instance = j;
			if (proc_compose_imsg(ps, src, i, IMSG_CTL_PROCFD,
			    -1, pa->pp_pipes[dst][j], &pf, sizeof(pf)) == -1)
				fatal("%s: proc_compose_imsg", __func__);

			/*
			 * We have to flush to send the descriptors and close
			 * them to avoid the fd ramp on startup.
			 */
			if (proc_flush_imsg(ps, src, i) == -1 ||
			    proc_flush_imsg(ps, dst, j) == -1)
				fatal("%s: imsgbuf_flush", __func__);
		}
	}
}

void
proc_close(struct privsep *ps)
{
	unsigned int		 dst, n;
	struct privsep_pipes	*pp;

	if (ps == NULL)
		return;

	pp = ps->ps_pp;

	for (dst = 0; dst < PROC_MAX; dst++) {
		if (ps->ps_ievs[dst] == NULL)
			continue;

		for (n = 0; n < ps->ps_instances[dst]; n++) {
			if (pp->pp_pipes[dst][n] == -1)
				continue;

			/* Cancel the fd, close and invalidate the fd */
			event_del(&(ps->ps_ievs[dst][n].ev));
			imsgbuf_clear(&(ps->ps_ievs[dst][n].ibuf));
			close(pp->pp_pipes[dst][n]);
			pp->pp_pipes[dst][n] = -1;
		}
		free(ps->ps_ievs[dst]);
	}
}

void
proc_shutdown(struct privsep_proc *p)
{
	struct privsep	*ps = p->p_ps;

	if (p->p_id == PROC_CONTROL && ps)
		control_cleanup(&ps->ps_csock);

	if (p->p_shutdown != NULL)
		(*p->p_shutdown)();

	proc_close(ps);

	log_info("%s exiting, pid %d", p->p_title, getpid());

	exit(0);
}

void
proc_sig_handler(int sig, short event, void *arg)
{
	struct privsep_proc	*p = arg;

	switch (sig) {
	case SIGINT:
	case SIGTERM:
		proc_shutdown(p);
		break;
	case SIGCHLD:
	case SIGHUP:
	case SIGPIPE:
	case SIGUSR1:
		/* ignore */
		break;
	default:
		fatalx("%s: unexpected signal", __func__);
		/* NOTREACHED */
	}
}

void
proc_run(struct privsep *ps, struct privsep_proc *p,
    struct privsep_proc *procs, unsigned int nproc,
    void (*run)(struct privsep *, struct privsep_proc *, void *), void *arg)
{
	struct passwd		*pw;
	const char		*root;
	struct control_sock	*rcs;

	log_procinit(p->p_title);

	if (p->p_id == PROC_CONTROL && ps->ps_instance == 0) {
		if (control_init(ps, &ps->ps_csock) == -1)
			fatalx("%s: control_init", __func__);
		TAILQ_FOREACH(rcs, &ps->ps_rcsocks, cs_entry)
			if (control_init(ps, rcs) == -1)
				fatalx("%s: control_init", __func__);
	}

	/* Use non-standard user */
	if (p->p_pw != NULL)
		pw = p->p_pw;
	else
		pw = ps->ps_pw;

	/* Change root directory */
	if (p->p_chroot != NULL)
		root = p->p_chroot;
	else
		root = pw->pw_dir;

	if (chroot(root) == -1)
		fatal("%s: chroot", __func__);
	if (chdir("/") == -1)
		fatal("%s: chdir(\"/\")", __func__);

	privsep_process = p->p_id;

	setproctitle("%s", p->p_title);

	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		fatal("%s: cannot drop privileges", __func__);

	event_init();

	signal_set(&ps->ps_evsigint, SIGINT, proc_sig_handler, p);
	signal_set(&ps->ps_evsigterm, SIGTERM, proc_sig_handler, p);
	signal_set(&ps->ps_evsigchld, SIGCHLD, proc_sig_handler, p);
	signal_set(&ps->ps_evsighup, SIGHUP, proc_sig_handler, p);
	signal_set(&ps->ps_evsigpipe, SIGPIPE, proc_sig_handler, p);
	signal_set(&ps->ps_evsigusr1, SIGUSR1, proc_sig_handler, p);

	signal_add(&ps->ps_evsigint, NULL);
	signal_add(&ps->ps_evsigterm, NULL);
	signal_add(&ps->ps_evsigchld, NULL);
	signal_add(&ps->ps_evsighup, NULL);
	signal_add(&ps->ps_evsigpipe, NULL);
	signal_add(&ps->ps_evsigusr1, NULL);

	proc_setup(ps, procs, nproc);
	proc_accept(ps, PROC_PARENT_SOCK_FILENO, PROC_PARENT, 0);
	if (p->p_id == PROC_CONTROL && ps->ps_instance == 0) {
		if (control_listen(&ps->ps_csock) == -1)
			fatalx("%s: control_listen", __func__);
		TAILQ_FOREACH(rcs, &ps->ps_rcsocks, cs_entry)
			if (control_listen(rcs) == -1)
				fatalx("%s: control_listen", __func__);
	}

	DPRINTF("%s: %s %d/%d, pid %d", __func__, p->p_title,
	    ps->ps_instance + 1, ps->ps_instances[p->p_id], getpid());

	if (run != NULL)
		run(ps, p, arg);

	event_dispatch();

	proc_shutdown(p);
}

void
proc_dispatch(int fd, short event, void *arg)
{
	struct imsgev		*iev = arg;
	struct privsep_proc	*p = iev->proc;
	struct privsep		*ps = p->p_ps;
	struct imsgbuf		*ibuf;
	struct imsg		 imsg;
	ssize_t			 n;
	int			 verbose;
	const char		*title;
	struct privsep_fd	 pf;

	title = ps->ps_title[privsep_process];
	ibuf = &iev->ibuf;

	if (event & EV_READ) {
		if ((n = imsgbuf_read(ibuf)) == -1)
			fatal("%s: imsgbuf_read", __func__);
		if (n == 0) {
			/* this pipe is dead, so remove the event handler */
			event_del(&iev->ev);
			event_loopexit(NULL);
			return;
		}
	}

	if (event & EV_WRITE) {
		if (imsgbuf_write(ibuf) == -1) {
			if (errno == EPIPE) {	/* connection closed */
				/* remove the event handler */
				event_del(&iev->ev);
				event_loopexit(NULL);
				return;
			} else
				fatal("%s: imsgbuf_write", __func__);
		}
	}

	for (;;) {
		if ((n = imsg_get(ibuf, &imsg)) == -1)
			fatal("%s: imsg_get", __func__);
		if (n == 0)
			break;

#if DEBUG > 1
		log_debug("%s: %s %d got imsg %d peerid %d from %s %d",
		    __func__, title, ps->ps_instance + 1,
		    imsg.hdr.type, imsg.hdr.peerid, p->p_title, imsg.hdr.pid);
#endif

		/*
		 * Check the message with the program callback
		 */
		if ((p->p_cb)(fd, p, &imsg) == 0) {
			/* Message was handled by the callback, continue */
			imsg_free(&imsg);
			continue;
		}

		/*
		 * Generic message handling
		 */
		switch (imsg.hdr.type) {
		case IMSG_CTL_VERBOSE:
			IMSG_SIZE_CHECK(&imsg, &verbose);
			memcpy(&verbose, imsg.data, sizeof(verbose));
			log_setverbose(verbose);
			break;
		case IMSG_CTL_PROCFD:
			IMSG_SIZE_CHECK(&imsg, &pf);
			memcpy(&pf, imsg.data, sizeof(pf));
			proc_accept(ps, imsg_get_fd(&imsg), pf.pf_procid,
			    pf.pf_instance);
			break;
		default:
			fatalx("%s: %s %d got invalid imsg %d peerid %d "
			    "from %s %d",
			    __func__, title, ps->ps_instance + 1,
			    imsg.hdr.type, imsg.hdr.peerid,
			    p->p_title, imsg.hdr.pid);
		}
		imsg_free(&imsg);
	}
	imsg_event_add(iev);
}

int
proc_dispatch_null(int fd, struct privsep_proc *p, struct imsg *imsg)
{
	return (-1);
}

/*
 * imsg helper functions
 */

void
imsg_event_add(struct imsgev *iev)
{
	if (iev->handler == NULL) {
		imsgbuf_flush(&iev->ibuf);
		return;
	}

	iev->events = EV_READ;
	if (imsgbuf_queuelen(&iev->ibuf) > 0)
		iev->events |= EV_WRITE;

	event_del(&iev->ev);
	event_set(&iev->ev, iev->ibuf.fd, iev->events, iev->handler, iev->data);
	event_add(&iev->ev, NULL);
}

int
imsg_compose_event(struct imsgev *iev, uint16_t type, uint32_t peerid,
    pid_t pid, int fd, void *data, uint16_t datalen)
{
	int	ret;

	if ((ret = imsg_compose(&iev->ibuf, type, peerid,
	    pid, fd, data, datalen)) == -1)
		return (ret);
	imsg_event_add(iev);
	return (ret);
}

int
imsg_composev_event(struct imsgev *iev, uint16_t type, uint32_t peerid,
    pid_t pid, int fd, const struct iovec *iov, int iovcnt)
{
	int	ret;

	if ((ret = imsg_composev(&iev->ibuf, type, peerid,
	    pid, fd, iov, iovcnt)) == -1)
		return (ret);
	imsg_event_add(iev);
	return (ret);
}

void
proc_range(struct privsep *ps, enum privsep_procid id, int *n, int *m)
{
	if (*n == -1) {
		/* Use a range of all target instances */
		*n = 0;
		*m = ps->ps_instances[id];
	} else {
		/* Use only a single slot of the specified peer process */
		*m = *n + 1;
	}
}

int
proc_compose_imsg(struct privsep *ps, enum privsep_procid id, int n,
    uint16_t type, uint32_t peerid, int fd, void *data, uint16_t datalen)
{
	int	 m;

	proc_range(ps, id, &n, &m);
	for (; n < m; n++) {
		if (imsg_compose_event(&ps->ps_ievs[id][n],
		    type, peerid, ps->ps_instance + 1, fd, data, datalen) == -1)
			return (-1);
	}

	return (0);
}

int
proc_compose(struct privsep *ps, enum privsep_procid id,
    uint16_t type, void *data, uint16_t datalen)
{
	return (proc_compose_imsg(ps, id, -1, type, -1, -1, data, datalen));
}

int
proc_composev_imsg(struct privsep *ps, enum privsep_procid id, int n,
    uint16_t type, uint32_t peerid, int fd, const struct iovec *iov, int iovcnt)
{
	int	 m;

	proc_range(ps, id, &n, &m);
	for (; n < m; n++)
		if (imsg_composev_event(&ps->ps_ievs[id][n],
		    type, peerid, ps->ps_instance + 1, fd, iov, iovcnt) == -1)
			return (-1);

	return (0);
}

int
proc_composev(struct privsep *ps, enum privsep_procid id,
    uint16_t type, const struct iovec *iov, int iovcnt)
{
	return (proc_composev_imsg(ps, id, -1, type, -1, -1, iov, iovcnt));
}

int
proc_forward_imsg(struct privsep *ps, struct imsg *imsg,
    enum privsep_procid id, int n)
{
	return (proc_compose_imsg(ps, id, n, imsg->hdr.type,
	    imsg->hdr.peerid, -1, imsg->data, IMSG_DATA_SIZE(imsg)));
}

struct imsgbuf *
proc_ibuf(struct privsep *ps, enum privsep_procid id, int n)
{
	int	 m;

	proc_range(ps, id, &n, &m);
	return (&ps->ps_ievs[id][n].ibuf);
}

struct imsgev *
proc_iev(struct privsep *ps, enum privsep_procid id, int n)
{
	int	 m;

	proc_range(ps, id, &n, &m);
	return (&ps->ps_ievs[id][n]);
}

/* This function should only be called with care as it breaks async I/O */
int
proc_flush_imsg(struct privsep *ps, enum privsep_procid id, int n)
{
	struct imsgbuf	*ibuf;
	int		 m, ret = 0;

	proc_range(ps, id, &n, &m);
	for (; n < m; n++) {
		if ((ibuf = proc_ibuf(ps, id, n)) == NULL)
			return (-1);
		if ((ret = imsgbuf_flush(ibuf)) == -1)
			break;
		imsg_event_add(&ps->ps_ievs[id][n]);
	}

	return (ret);
}
