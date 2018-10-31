/*
 * system calls hijack code
 * Copyright (c) 2015 Hajime Tazaki
 *
 * Author: Hajime Tazaki <tazaki@sfc.wide.ad.jp>
 *
 * Note: some of the code is picked from rumpkernel, written by Antti Kantee.
 */

#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/mman.h>
#define __USE_GNU
#include <dlfcn.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/epoll.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <assert.h>
#include <pthread.h>
#include <lkl.h>
#include <lkl_host.h>

#include "xlate.h"
#include "init.h"

static int is_lklfd(int fd)
{
	if (fd < LKL_FD_OFFSET)
		return 0;

	return 1;
}

static void *resolve_sym(const char *sym)
{
	void *resolv;

	resolv = dlsym(RTLD_NEXT, sym);
	if (!resolv) {
		fprintf(stderr, "dlsym fail %s (%s)\n", sym, dlerror());
		assert(0);
	}
	return resolv;
}

typedef long (*host_call)(long p1, long p2, long p3, long p4, long p5, long p6);

static host_call host_calls[__lkl__NR_syscalls];
/* internally managed fd list for epoll */
int dual_fds[LKL_FD_OFFSET];

#define HOOK_FD_CALL(name)						\
	static void __attribute__((constructor(101)))			\
	init_host_##name(void)						\
	{								\
		host_calls[__lkl__NR_##name] = resolve_sym(#name);	\
	}								\
									\
	long name##_hook(long p1, long p2, long p3, long p4, long p5,	\
			 long p6)					\
	{								\
		long p[6] = {p1, p2, p3, p4, p5, p6 };			\
									\
		if (!host_calls[__lkl__NR_##name])			\
			host_calls[__lkl__NR_##name] = resolve_sym(#name); \
		if (!is_lklfd(p1))					\
			return host_calls[__lkl__NR_##name](p1, p2, p3,	\
							    p4, p5, p6); \
									\
		return lkl_set_errno(lkl_syscall(__lkl__NR_##name, p));	\
	}								\
	asm(".global " #name);						\
	asm(".set " #name "," #name "_hook");				\

#define HOOK_CALL_USE_HOST_BEFORE_START(name)				\
	static void __attribute__((constructor(101)))			\
	init_host_##name(void)						\
	{								\
		host_calls[__lkl__NR_##name] = resolve_sym(#name);	\
	}								\
									\
	long name##_hook(long p1, long p2, long p3, long p4, long p5,	\
			 long p6)					\
	{								\
		long p[6] = {p1, p2, p3, p4, p5, p6 };			\
									\
		if (!host_calls[__lkl__NR_##name])			\
			host_calls[__lkl__NR_##name] = resolve_sym(#name); \
		if (!lkl_running)					\
			return host_calls[__lkl__NR_##name](p1, p2, p3,	\
							    p4, p5, p6); \
									\
		return lkl_set_errno(lkl_syscall(__lkl__NR_##name, p));	\
	}								\
	asm(".global " #name);						\
	asm(".set " #name "," #name "_hook")

#define HOST_CALL(name)							\
	static long (*host_##name)();					\
	static void __attribute__((constructor(101)))			\
	init2_host_##name(void)						\
	{								\
		host_##name = resolve_sym(#name);			\
	}

#define HOOK_CALL(name)							\
	long name##_hook(long p1, long p2, long p3, long p4, long p5,	\
			 long p6)					\
	{								\
		long p[6] = {p1, p2, p3, p4, p5, p6};			\
									\
		return lkl_set_errno(lkl_syscall(__lkl__NR_##name, p));	\
	}								\
	asm(".global " #name);						\
	asm(".set " #name "," #name "_hook");				\

#define CHECK_HOST_CALL(name)				\
	if (!host_##name)				\
		host_##name = resolve_sym(#name)

static int lkl_call(int nr, int args, ...)
{
	long params[6];
	va_list vl;
	int i;

	va_start(vl, args);
	for (i = 0; i < args; i++)
		params[i] = va_arg(vl, long);
	va_end(vl);

	return lkl_set_errno(lkl_syscall(nr, params));
}

HOOK_FD_CALL(recvmsg)
HOOK_FD_CALL(sendmsg)
HOOK_FD_CALL(sendmmsg)
HOOK_FD_CALL(getsockname)
HOOK_FD_CALL(getpeername)
HOOK_FD_CALL(bind)
HOOK_FD_CALL(connect)
HOOK_FD_CALL(listen)
HOOK_FD_CALL(shutdown)
HOOK_FD_CALL(accept)
HOOK_FD_CALL(write)
HOOK_FD_CALL(writev)
HOOK_FD_CALL(sendto)
HOOK_FD_CALL(read)
HOOK_FD_CALL(readv)
HOOK_FD_CALL(recvfrom)
HOOK_FD_CALL(splice)
HOOK_FD_CALL(vmsplice)

HOOK_CALL_USE_HOST_BEFORE_START(accept4);
HOOK_CALL_USE_HOST_BEFORE_START(pipe2);

HOST_CALL(write)
HOST_CALL(pipe2)

HOST_CALL(setsockopt);
int setsockopt(int fd, int level, int optname, const void *optval,
	       socklen_t optlen)
{
	CHECK_HOST_CALL(setsockopt);
	if (!is_lklfd(fd))
		return host_setsockopt(fd, level, optname, optval, optlen);
	return lkl_call(__lkl__NR_setsockopt, 5, fd, lkl_solevel_xlate(level),
			lkl_soname_xlate(optname), (void*)optval, optlen);
}

HOST_CALL(getsockopt);
int getsockopt(int fd, int level, int optname, void *optval, socklen_t *optlen)
{
	CHECK_HOST_CALL(getsockopt);
	if (!is_lklfd(fd))
		return host_getsockopt(fd, level, optname, optval, optlen);
	return lkl_call(__lkl__NR_getsockopt, 5, fd, lkl_solevel_xlate(level),
			lkl_soname_xlate(optname), optval, (int*)optlen);
}

HOST_CALL(socket);
int socket(int domain, int type, int protocol)
{
	CHECK_HOST_CALL(socket);
	if (domain == AF_UNIX || domain == PF_PACKET)
		return host_socket(domain, type, protocol);

	if (!lkl_running)
		return host_socket(domain, type, protocol);

	return lkl_call(__lkl__NR_socket, 3, domain, type, protocol);
}

HOST_CALL(ioctl);
#ifdef __ANDROID__
int ioctl(int fd, int req, ...)
#else
int ioctl(int fd, unsigned long req, ...)
#endif
{
	va_list vl;
	long arg;

	va_start(vl, req);
	arg = va_arg(vl, long);
	va_end(vl);

	CHECK_HOST_CALL(ioctl);

	if (!is_lklfd(fd))
		return host_ioctl(fd, req, arg);
	return lkl_call(__lkl__NR_ioctl, 3, fd, lkl_ioctl_req_xlate(req), arg);
}


HOST_CALL(fcntl);
int fcntl(int fd, int cmd, ...)
{
	va_list vl;
	long arg;

	va_start(vl, cmd);
	arg = va_arg(vl, long);
	va_end(vl);

	CHECK_HOST_CALL(fcntl);

	if (!is_lklfd(fd))
		return host_fcntl(fd, cmd, arg);
	return lkl_call(__lkl__NR_fcntl, 3, fd, lkl_fcntl_cmd_xlate(cmd), arg);
}

HOST_CALL(poll);
int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
	unsigned int i, lklfds = 0, hostfds = 0;

	CHECK_HOST_CALL(poll);

	for (i = 0; i < nfds; i++) {
		if (is_lklfd(fds[i].fd))
			lklfds = 1;
		else
			hostfds = 1;
	}

	/* FIXME: need to handle mixed case of hostfd and lklfd. */
	if (lklfds && hostfds)
		return lkl_set_errno(-LKL_EOPNOTSUPP);


	if (hostfds)
		return host_poll(fds, nfds, timeout);

	return lkl_sys_poll((struct lkl_pollfd *)fds, nfds, timeout);
}

int __poll(struct pollfd *, nfds_t, int) __attribute__((alias("poll")));

HOST_CALL(select);
int select(int nfds, fd_set *r, fd_set *w, fd_set *e, struct timeval *t)
{
	int fd, hostfds = 0, lklfds = 0;

	CHECK_HOST_CALL(select);

	for (fd = 0; fd < nfds; fd++) {
		if (r != 0 && FD_ISSET(fd, r)) {
			if (is_lklfd(fd))
				lklfds = 1;
			else
				hostfds = 1;
		}
		if (w != 0 && FD_ISSET(fd, w)) {
			if (is_lklfd(fd))
				lklfds = 1;
			else
				hostfds = 1;
		}
		if (e != 0 && FD_ISSET(fd, e)) {
			if (is_lklfd(fd))
				lklfds = 1;
			else
				hostfds = 1;
		}
	}

	/* FIXME: handle mixed case of hostfd and lklfd */
	if (lklfds && hostfds)
		return lkl_set_errno(-LKL_EOPNOTSUPP);

	if (hostfds)
		return host_select(nfds, r, w, e, t);

	return lkl_sys_select(nfds, (lkl_fd_set *)r, (lkl_fd_set *)w,
			      (lkl_fd_set *)e, (struct lkl_timeval *)t);
}

HOST_CALL(close);
int close(int fd)
{
	CHECK_HOST_CALL(close);

	if (!is_lklfd(fd)) {
		/* handle epoll's dual_fd */
		if ((dual_fds[fd] != -1) && lkl_running) {
			lkl_call(__lkl__NR_close, 1, dual_fds[fd]);
			dual_fds[fd] = -1;
		}

		return host_close(fd);
	}

	return lkl_call(__lkl__NR_close, 1, fd);
}

HOST_CALL(epoll_create);
int epoll_create(int size)
{
	int host_fd;

	CHECK_HOST_CALL(epoll_create);

	host_fd = host_epoll_create(size);
	if (!host_fd) {
		fprintf(stderr, "%s fail (%d)\n", __func__, errno);
		return -1;
	}

	if (!lkl_running)
		return host_fd;

	dual_fds[host_fd] = lkl_sys_epoll_create(size);

	/* always returns the host fd */
	return host_fd;
}

HOST_CALL(epoll_create1);
int epoll_create1(int flags)
{
	int host_fd;

	CHECK_HOST_CALL(epoll_create1);

	host_fd = host_epoll_create1(flags);
	if (!host_fd) {
		fprintf(stderr, "%s fail (%d)\n", __func__, errno);
		return -1;
	}

	if (!lkl_running)
		return host_fd;

	dual_fds[host_fd] = lkl_sys_epoll_create1(flags);

	/* always returns the host fd */
	return host_fd;
}


HOST_CALL(epoll_ctl);
int epoll_ctl(int epollfd, int op, int fd, struct epoll_event *event)
{
	CHECK_HOST_CALL(epoll_ctl);

	if (!is_lklfd(fd))
		return host_epoll_ctl(epollfd, op, fd, event);

	return lkl_call(__lkl__NR_epoll_ctl, 4, dual_fds[epollfd],
			op, fd, event);
}

struct epollarg {
	int epfd;
	struct epoll_event *events;
	int maxevents;
	int timeout;
	int pipefd;
	int errnum;
};

HOST_CALL(epoll_wait)
static void *host_epollwait(void *arg)
{
	struct epollarg *earg = arg;
	int ret;

	ret = host_epoll_wait(earg->epfd, earg->events,
			      earg->maxevents, earg->timeout);
	if (ret == -1)
		earg->errnum = errno;
	lkl_call(__lkl__NR_write, 3, earg->pipefd, &ret, sizeof(ret));

	return (void *)(intptr_t)ret;
}

int epoll_wait(int epfd, struct epoll_event *events,
	       int maxevents, int timeout)
{
	CHECK_HOST_CALL(epoll_wait);
	CHECK_HOST_CALL(pipe2);

	int l_pipe[2] = {-1, -1}, h_pipe[2] = {-1, -1};
	struct epoll_event host_ev, lkl_ev;
	int ret_events = maxevents;
	struct epoll_event h_events[ret_events], l_events[ret_events];
	struct epollarg earg;
	pthread_t thread;
	void *trv_val;
	int i, ret, ret_lkl, ret_host;

	ret = lkl_sys_pipe(l_pipe);
	if (ret == -1) {
		fprintf(stderr, "lkl pipe error(errno=%d)\n", errno);
		return -1;
	}

	ret = host_pipe2(h_pipe, 0);
	if (ret == -1) {
		fprintf(stderr, "host pipe error(errno=%d)\n", errno);
		return -1;
	}

	if (dual_fds[epfd] == -1) {
		fprintf(stderr, "epollfd isn't available (%d)\n", epfd);
		abort();
	}

	/* wait pipe at host/lkl epoll_fd */
	memset(&lkl_ev, 0, sizeof(lkl_ev));
	lkl_ev.events = EPOLLIN;
	lkl_ev.data.fd = l_pipe[0];
	ret = lkl_call(__lkl__NR_epoll_ctl, 4, dual_fds[epfd], EPOLL_CTL_ADD,
		       l_pipe[0], &lkl_ev);
	if (ret == -1) {
		fprintf(stderr, "epoll_ctl error(epfd=%d:%d, fd=%d, err=%d)\n",
			epfd, dual_fds[epfd], l_pipe[0], errno);
		return -1;
	}

	memset(&host_ev, 0, sizeof(host_ev));
	host_ev.events = EPOLLIN;
	host_ev.data.fd = h_pipe[0];
	ret = host_epoll_ctl(epfd, EPOLL_CTL_ADD, h_pipe[0], &host_ev);
	if (ret == -1) {
		fprintf(stderr, "host epoll_ctl error(%d, %d, %d, %d)\n",
			epfd, h_pipe[0], h_pipe[1], errno);
		return -1;
	}


	/* now wait by epoll_wait on 2 threads */
	memset(h_events, 0, sizeof(struct epoll_event) * ret_events);
	memset(l_events, 0, sizeof(struct epoll_event) * ret_events);
	earg.epfd = epfd;
	earg.events = h_events;
	earg.maxevents = maxevents;
	earg.timeout = timeout;
	earg.pipefd = l_pipe[1];
	pthread_create(&thread, NULL, host_epollwait, &earg);

	ret_lkl = lkl_sys_epoll_wait(dual_fds[epfd],
				     (struct lkl_epoll_event *)l_events,
				     maxevents, timeout);
	if (ret_lkl == -1) {
		fprintf(stderr,
			"lkl_%s_wait error(epfd=%d:%d, fd=%d, err=%d)\n",
			__func__, epfd, dual_fds[epfd], l_pipe[0], errno);
		return -1;
	}
	host_write(h_pipe[1], &ret, sizeof(ret));
	pthread_join(thread, &trv_val);
	ret_host = (int)(intptr_t)trv_val;
	if (ret_host == -1) {
		fprintf(stderr,
			"host epoll_ctl error(%d, %d, %d, %d)\n", epfd,
			h_pipe[0], h_pipe[1], errno);
		return -1;
	}

	ret = lkl_call(__lkl__NR_epoll_ctl, 4, dual_fds[epfd], EPOLL_CTL_DEL,
		       l_pipe[0], &lkl_ev);
	if (ret == -1) {
		fprintf(stderr,
			"lkl epoll_ctl error(epfd=%d:%d, fd=%d, err=%d)\n",
			epfd, dual_fds[epfd], l_pipe[0], errno);
		return -1;
	}

	ret = host_epoll_ctl(epfd, EPOLL_CTL_DEL, h_pipe[0], &host_ev);
	if (ret == -1) {
		fprintf(stderr, "host epoll_ctl error(%d, %d, %d, %d)\n",
			epfd, h_pipe[0], h_pipe[1], errno);
		return -1;
	}

	memset(events, 0, sizeof(struct epoll_event) * maxevents);
	ret = 0;
	if (ret_host > 0) {
		for (i = 0; i < ret_host; i++) {
			if (h_events[i].data.fd == h_pipe[0])
				continue;
			if (is_lklfd(h_events[i].data.fd))
				continue;

			memcpy(events, &(h_events[i]),
			       sizeof(struct epoll_event));
			events++;
			ret++;
		}
	}
	if (ret_lkl > 0) {
		for (i = 0; i < ret_lkl; i++) {
			if (l_events[i].data.fd == l_pipe[0])
				continue;
			if (!is_lklfd(l_events[i].data.fd))
				continue;

			memcpy(events, &(l_events[i]),
			       sizeof(struct epoll_event));
			events++;
			ret++;
		}
	}

	lkl_call(__lkl__NR_close, 1, l_pipe[0]);
	lkl_call(__lkl__NR_close, 1, l_pipe[1]);
	host_close(h_pipe[0]);
	host_close(h_pipe[1]);

	return ret;
}

int eventfd(unsigned int count, int flags)
{
	if (!lkl_running) {
		int (*f)(unsigned int, int) = resolve_sym("eventfd");

		return f(count, flags);
	}

	return lkl_sys_eventfd2(count, flags);
}

HOST_CALL(eventfd_read);
int eventfd_read(int fd, uint64_t *value)
{
	CHECK_HOST_CALL(eventfd_read);

	if (!is_lklfd(fd))
		return host_eventfd_read(fd, value);

	return lkl_sys_read(fd, (void *) value,
			    sizeof(*value)) != sizeof(*value) ? -1 : 0;
}

HOST_CALL(eventfd_write);
int eventfd_write(int fd, uint64_t value)
{
	CHECK_HOST_CALL(eventfd_write);

	if (!is_lklfd(fd))
		return host_eventfd_write(fd, value);

	return lkl_sys_write(fd, (void *) &value,
			     sizeof(value)) != sizeof(value) ? -1 : 0;
}

HOST_CALL(mmap)
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
	CHECK_HOST_CALL(mmap);

	if (addr != NULL || flags != (MAP_ANONYMOUS|MAP_PRIVATE) ||
	    prot != (PROT_READ|PROT_WRITE) || fd != -2 || offset != 0)
		return (void *)host_mmap(addr, length, prot, flags, fd, offset);
	return lkl_sys_mmap(addr, length, prot, flags, fd, offset);
}

#ifndef __ANDROID__
HOST_CALL(__xstat64)
int stat(const char *pathname, struct stat *buf)
{
	CHECK_HOST_CALL(__xstat64);
	return host___xstat64(0, pathname, buf);
}
#endif

ssize_t send(int fd, const void *buf, size_t len, int flags)
{
	return sendto(fd, buf, len, flags, 0, 0);
}

ssize_t recv(int fd, void *buf, size_t len, int flags)
{
	return recvfrom(fd, buf, len, flags, 0, 0);
}

extern int pipe2(int fd[2], int flag);
int pipe(int fd[2])
{
	if (!lkl_running)
		return host_calls[__lkl__NR_pipe2]((long)fd, 0, 0, 0, 0, 0);

	return pipe2(fd, 0);

}
