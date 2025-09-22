/*	$OpenBSD: proc.c,v 1.36 2025/08/14 10:50:08 jsg Exp $	*/

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
#include <pwd.h>
#include <event.h>
#include <imsg.h>
#include <ctype.h>

#include "proc.h"

void	 proc_exec(struct privsep *, struct privsep_proc *, unsigned int, int,
	    char **);
void	 proc_setup(struct privsep *, struct privsep_proc *, unsigned int);
void	 proc_accept(struct privsep *, int, enum privsep_procid);
void	 proc_close(struct privsep *);
void	 proc_shutdown(struct privsep_proc *);
void	 proc_sig_handler(int, short, void *);
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
	int			 fds[2];

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
	for (i = 1; i < (unsigned int) argc; i++)
		nargv[nargc++] = argv[i];

	nargv[nargc] = NULL;

	for (proc = 0; proc < nproc; proc++) {
		p = &procs[proc];

		/* Update args with process title. */
		nargv[proc_i] = (char *)(uintptr_t)p->p_title;

		/* Create socket for communication back to the Parent. */
		if (socketpair(AF_UNIX, SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,
		    PF_UNSPEC, fds) == -1)
			fatal("socketpair");

		/* Fire up child process. */
		switch (fork()) {
		case -1:
			fatal("%s: fork", __func__);
			break;
		case 0:
			close(fds[0]);
			/* Prepare parent socket. */
			if (fds[1] != PROC_PARENT_SOCK_FILENO) {
				if (dup2(fds[1], PROC_PARENT_SOCK_FILENO) == -1)
					fatal("dup2");
			} else if (fcntl(fds[1], F_SETFD, 0) == -1)
				fatal("fcntl");

			execvp(argv[0], nargv);
			fatal("%s: execvp", __func__);
			break;
		default:
			ps->ps_pipes[p->p_id] = fds[0];
			close(fds[1]);
			break;
		}
	}
	free(nargv);
}

void
proc_connect(struct privsep *ps)
{
	struct imsgev		*iev;
	enum privsep_procid	dst;

	/* Don't distribute any sockets if we are not really going to run. */
	if (ps->ps_noaction)
		return;

	for (dst = 0; dst < PROC_MAX; dst++) {
		/* We don't communicate with ourselves. */
		if (dst == PROC_PARENT)
			continue;

		iev = &ps->ps_ievs[dst];
		if (imsgbuf_init(&iev->ibuf, ps->ps_pipes[dst]) == -1)
			fatal("imsgbuf_init");
		imsgbuf_allow_fdpass(&iev->ibuf);
		event_set(&iev->ev, iev->ibuf.fd, iev->events,
		    iev->handler, iev->data);
		event_add(&iev->ev, NULL);
	}
}

void
proc_init(struct privsep *ps, struct privsep_proc *procs, unsigned int nprocs,
    int debug, int argc, char **argv, enum privsep_procid proc_id)
{
	struct privsep_proc	*p = NULL;
	unsigned int		 proc;

	/* Don't initiate anything if we are not really going to run. */
	if (ps->ps_noaction)
		return;

	privsep_process = proc_id;

	if (proc_id == PROC_PARENT) {
		proc_setup(ps, procs, nprocs);

		if (!debug && daemon(0, 0) == -1)
			fatal("failed to daemonize");

		/* Engage! */
		proc_exec(ps, procs, nprocs, argc, argv);
		return;
	}

	/* Initialize a child */
	for (proc = 0; proc < nprocs; proc++) {
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
proc_accept(struct privsep *ps, int fd, enum privsep_procid dst)
{
	struct imsgev		*iev;

	if (ps->ps_ievs[dst].data == NULL) {
#if DEBUG > 1
		log_debug("%s: %s src %d to dst %d not connected",
		    __func__, ps->ps_title[privsep_process],
		    privsep_process, dst);
#endif
		close(fd);
		return;
	}

	if (ps->ps_pipes[dst] != -1) {
		log_warnx("%s: duplicated descriptor", __func__);
		close(fd);
		return;
	} else
		ps->ps_pipes[dst] = fd;

	iev = &ps->ps_ievs[dst];
	if (imsgbuf_init(&iev->ibuf, fd) == -1)
		fatal("imsgbuf_init");
	imsgbuf_allow_fdpass(&iev->ibuf);
	event_set(&iev->ev, iev->ibuf.fd, iev->events, iev->handler, iev->data);
	event_add(&iev->ev, NULL);
}

void
proc_setup(struct privsep *ps, struct privsep_proc *procs, unsigned int nproc)
{
	unsigned int		 dst, id;

	/* Initialize parent title, ps_instances and procs. */
	ps->ps_title[PROC_PARENT] = "vmd";

	for (dst = 0; dst < nproc; dst++) {
		procs[dst].p_ps = ps;
		if (procs[dst].p_cb == NULL)
			procs[dst].p_cb = proc_dispatch_null;

		id = procs[dst].p_id;
		ps->ps_title[id] = procs[dst].p_title;

		/* With this set up, we are ready to call imsgbuf_init(). */
		ps->ps_ievs[id].handler = proc_dispatch;
		ps->ps_ievs[id].events = EV_READ;
		ps->ps_ievs[id].proc = &procs[dst];

		/* Use the data field to indicate this imsgev is initialized. */
		ps->ps_ievs[id].data = &ps->ps_ievs[id];
	}

	/* Mark fds as unused */
	for (dst = 0; dst < PROC_MAX; dst++)
		ps->ps_pipes[dst] = -1;
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
	} while (pid != -1 || (pid == -1 && errno == EINTR));
}

void
proc_close(struct privsep *ps)
{
	unsigned int		 dst;

	if (ps == NULL)
		return;

	for (dst = 0; dst < PROC_MAX; dst++) {
		if (ps->ps_ievs[dst].data == NULL)
			continue;

		if (ps->ps_pipes[dst] == -1)
			continue;

		/* Cancel the fd, close and invalidate the fd */
		event_del(&(ps->ps_ievs[dst].ev));
		imsgbuf_clear(&(ps->ps_ievs[dst].ibuf));
		close(ps->ps_pipes[dst]);
		ps->ps_pipes[dst] = -1;

		/* Null the data field to indicate this imsgev is closed. */
		ps->ps_ievs[dst].data = NULL;
	}
}

void
proc_shutdown(struct privsep_proc *p)
{
	struct privsep	*ps = p->p_ps;

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

	log_procinit("%s", p->p_title);

	if (p->p_id == PROC_CONTROL) {
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
	proc_accept(ps, PROC_PARENT_SOCK_FILENO, PROC_PARENT);
	if (p->p_id == PROC_CONTROL) {
		if (control_listen(&ps->ps_csock) == -1)
			fatalx("%s: control_listen", __func__);
		TAILQ_FOREACH(rcs, &ps->ps_rcsocks, cs_entry)
			if (control_listen(rcs) == -1)
				fatalx("%s: control_listen", __func__);
	}

	DPRINTF("%s: %s, pid %d", __func__, p->p_title, getpid());

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
	uint32_t		 peer_id, type;
	pid_t			 pid;

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
			if (errno == EPIPE) {
				/* this pipe is dead, remove the handler */
				event_del(&iev->ev);
				event_loopexit(NULL);
				return;
			}
			fatal("%s: imsgbuf_write", __func__);
		}
	}

	for (;;) {
		if ((n = imsg_get(ibuf, &imsg)) == -1)
			fatal("%s: imsg_get", __func__);
		if (n == 0)
			break;

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
		type = imsg_get_type(&imsg);
		peer_id = imsg_get_id(&imsg);
		pid = imsg_get_pid(&imsg);

		switch (type) {
		case IMSG_CTL_VERBOSE:
			verbose = imsg_int_read(&imsg);
			log_setverbose(verbose);
			break;
		default:
			fatalx("%s: %s got invalid imsg %d peerid %d from %s "
			    "%d", __func__, title, type, peer_id, p->p_title,
			    pid);
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
	imsg_event_add2(iev, NULL);
}

void
imsg_event_add2(struct imsgev *iev, struct event_base *ev_base)
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
	if (ev_base != NULL)
		event_base_set(ev_base, &iev->ev);
	event_add(&iev->ev, NULL);
}

int
imsg_compose_event(struct imsgev *iev, uint32_t type, uint32_t peerid,
    pid_t pid, int fd, void *data, size_t datalen)
{
	return imsg_compose_event2(iev, type, peerid, pid, fd, data, datalen,
	    NULL);
}

int
imsg_compose_event2(struct imsgev *iev, uint32_t type, uint32_t peerid,
    pid_t pid, int fd, void *data, uint16_t datalen, struct event_base *ev_base)
{
	int	ret;

	ret = imsg_compose(&iev->ibuf, type, peerid, pid, fd, data, datalen);
	if (ret == -1)
		return (ret);
	imsg_event_add2(iev, ev_base);
	return (ret);
}

int
imsg_composev_event(struct imsgev *iev, uint32_t type, uint32_t peerid,
    pid_t pid, int fd, const struct iovec *iov, int iovcnt)
{
	int	ret;

	ret = imsg_composev(&iev->ibuf, type, peerid, pid, fd, iov, iovcnt);
	if (ret == -1)
		return (ret);
	imsg_event_add(iev);
	return (ret);
}

void
imsg_forward_event(struct imsgev *iev, struct imsg *imsg)
{
	if (imsg_forward(&iev->ibuf, imsg) == -1)
		fatalx("%s: imsg_forward", __func__);
	imsg_event_add(iev);
}

int
proc_compose_imsg(struct privsep *ps, enum privsep_procid id,
    uint32_t type, uint32_t peerid, int fd, void *data, size_t datalen)
{
	pid_t pid = 1;

	if (ps->ps_ievs[id].data == NULL)
		log_debug("%s: imsgev not initialized", __func__);

	if (imsg_compose_event(&ps->ps_ievs[id], type, peerid, pid, fd, data,
	    datalen) == -1)
		return (-1);

	return (0);
}

int
proc_compose(struct privsep *ps, enum privsep_procid id, uint32_t type,
    void *data, size_t datalen)
{
	return (proc_compose_imsg(ps, id, type, -1, -1, data, datalen));
}

int
proc_composev_imsg(struct privsep *ps, enum privsep_procid id, uint32_t type,
    uint32_t peerid, int fd, const struct iovec *iov, int iovcnt)
{
	pid_t pid = 1;
	if (imsg_composev_event(&ps->ps_ievs[id], type, peerid, pid, fd, iov,
	    iovcnt) == -1)
		return (-1);

	return (0);
}

int
proc_composev(struct privsep *ps, enum privsep_procid id,
    uint32_t type, const struct iovec *iov, int iovcnt)
{
	return (proc_composev_imsg(ps, id, type, -1, -1, iov, iovcnt));
}

int
proc_forward_imsg(struct privsep *ps, struct imsg *imsg, enum privsep_procid id,
    uint32_t new_peerid)
{
	int		 fd, ret;
	size_t		 sz;
	uint32_t	 peerid, type;
	void		*data = NULL;

	fd = imsg_get_fd(imsg);
	sz = imsg_get_len(imsg);
	type = imsg_get_type(imsg);

	if (new_peerid == (uint32_t)(-1))
		peerid = imsg_get_id(imsg);
	else
		peerid = new_peerid;

	if (sz > 0) {
		data = malloc(sz);
		if (data == NULL)
			return (ENOMEM);
		if (imsg_get_data(imsg, data, sz))
			fatal("%s: imsg_get_data", __func__);
	}

	ret = proc_compose_imsg(ps, id, type, peerid, fd, data, sz);
	if (sz > 0)
		free(data);
	return (ret);
}

/* This function should only be called with care as it breaks async I/O */
int
proc_flush_imsg(struct privsep *ps, enum privsep_procid id)
{
	int		 ret = 0;

	ret = imsgbuf_flush(&ps->ps_ievs[id].ibuf);
	if (ret == 0)
		imsg_event_add(&ps->ps_ievs[id]);

	return (ret);
}

unsigned int
imsg_uint_read(struct imsg *imsg)
{
	unsigned int val;

	if (imsg_get_data(imsg, &val, sizeof(val)))
		fatal("%s", __func__);

	return (val);
}

int
imsg_int_read(struct imsg *imsg)
{
	int val;

	if (imsg_get_data(imsg, &val, sizeof(val)))
		fatal("%s", __func__);

	return (val);
}

char *
imsg_string_read(struct imsg *imsg, size_t max)
{
	size_t i, sz;
	char *s = NULL;

	sz = imsg_get_len(imsg);
	if (sz > max) {
		log_warnx("%s: string too large", __func__);
		return (NULL);
	}
	s = malloc(sz);
	if (imsg_get_data(imsg, s, sz))
		fatal("%s: imsg_get_data", __func__);

	/* Guarantee NUL-termination. */
	s[sz - 1] = '\0';

	/* Ensure all characters are printable. */
	for (i = 0; i < sz; i++) {
		if (s[i] == '\0')
			break;
		if (!isprint(s[i])) {
			log_warnx("%s: non-printable character", __func__);
			free(s);
			return (NULL);
		}
	}

	return (s);
}
