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

HOOK_FD_CALL(close)
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
HOOK_FD_CALL(send)
HOOK_FD_CALL(read)
HOOK_FD_CALL(recvfrom)
HOOK_FD_CALL(recv)
HOOK_FD_CALL(epoll_wait)
HOOK_FD_CALL(splice)
HOOK_FD_CALL(vmsplice)
HOOK_CALL_USE_HOST_BEFORE_START(pipe);

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
		return host_setsockopt(fd, level, optname, optval, optlen);
	return lkl_call(__lkl__NR_getsockopt, 5, fd, lkl_solevel_xlate(level),
			lkl_soname_xlate(optname), optval, (int*)optlen);
}

HOST_CALL(socket);
int socket(int domain, int type, int protocol)
{
	CHECK_HOST_CALL(socket);
	if (domain == AF_UNIX || domain == PF_PACKET)
		return host_socket(domain, type, protocol);

	return lkl_call(__lkl__NR_socket, 3, domain, type, protocol);
}

HOST_CALL(ioctl);
int ioctl(int fd, unsigned long req, ...)
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

	return lkl_call(__lkl__NR_poll, 3, fds, nfds, timeout);
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

	return lkl_call(__lkl__NR_select, 5, nfds, r, w, e, t);
}

HOOK_CALL_USE_HOST_BEFORE_START(epoll_create);

HOST_CALL(epoll_ctl);
int epoll_ctl(int epollfd, int op, int fd, struct epoll_event *event)
{
	CHECK_HOST_CALL(epoll_ctl);

	if (is_lklfd(epollfd) != is_lklfd(fd))
		return lkl_set_errno(-LKL_EOPNOTSUPP);

	if (!is_lklfd(epollfd))
		return host_epoll_ctl(epollfd, op, fd, event);

	return lkl_call(__lkl__NR_epoll_ctl, 4, epollfd, op, fd, event);
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
