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
#define __USE_GNU
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <time.h>
#include <sys/ioctl.h>
#include <net/if.h>

#include "hostcalls.h"
#undef st_atime
#undef st_mtime
#undef st_ctime
#include <lkl.h>
#include <lkl_host.h>

/* fd number offset */
#define LKL_FD_OFFSET (256/2)

int socket(int domain, int type, int protocol)
{
	int ret;

	if (domain == AF_UNIX) {
		if (!host_socket)
			hostcall_init();
		return host_socket(domain, type, protocol);
	}

	ret = lkl_sys_socket(domain, type, protocol);
	if (ret >= 0)
		return ret + LKL_FD_OFFSET;
	return ret;
}

int close(int fd)
{
	if (fd < LKL_FD_OFFSET) {
		if (!host_close)
			hostcall_init();
		return host_close(fd);
	}

	return lkl_sys_close(fd - LKL_FD_OFFSET);
}

ssize_t recvmsg(int fd, struct msghdr *msghdr, int flags)
{
	return lkl_sys_recvmsg(fd - LKL_FD_OFFSET, msghdr, flags);
}

ssize_t sendmsg(int fd, const struct msghdr *msghdr, int flags)
{
	if (fd < LKL_FD_OFFSET) {
		if (!host_sendmsg)
			hostcall_init();
		return host_sendmsg(fd, msghdr, flags);
	}
	return lkl_sys_sendmsg(fd - LKL_FD_OFFSET,
				       (struct msghdr *)msghdr, flags);
}

int sendmmsg(int fd, struct mmsghdr *msghdr, unsigned int vlen,
		  int flags)
{
	return lkl_sys_sendmmsg(fd - LKL_FD_OFFSET, msghdr,
					vlen, flags);
}

int getsockname(int fd, struct sockaddr *name, socklen_t *namelen)
{
	return lkl_sys_getsockname(fd - LKL_FD_OFFSET, name,
					   (int *)namelen);
}

int getpeername(int fd, struct sockaddr *name, socklen_t *namelen)
{
	return lkl_sys_getpeername(fd - LKL_FD_OFFSET, name,
					   (int *)namelen);
}

int bind(int fd, const struct sockaddr *name, socklen_t namelen)
{
	if (fd < LKL_FD_OFFSET)
		return host_bind(fd, name, namelen);

	return lkl_sys_bind(fd - LKL_FD_OFFSET, (struct sockaddr *)name,
				    namelen);
}

int connect(int fd, const struct sockaddr *addr, socklen_t len)
{
	return lkl_sys_connect(fd - LKL_FD_OFFSET,
				       (struct sockaddr *)addr, len);
}

int listen(int fd, int backlog)
{
	if (fd < LKL_FD_OFFSET)
		return host_listen(fd, backlog);
	return lkl_sys_listen(fd - LKL_FD_OFFSET, backlog);
}

int shutdown(int fd, int how)
{
	if (fd < LKL_FD_OFFSET)
		return host_shutdown(fd, how);
	return lkl_sys_shutdown(fd - LKL_FD_OFFSET, how);
}

int accept(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
	if (fd < LKL_FD_OFFSET)
		return host_accept(fd, addr, addrlen);
	return lkl_sys_accept(fd - LKL_FD_OFFSET, addr, (int *)addrlen);
}

ssize_t write(int fd, const void *buf, size_t count)
{
	if (fd < LKL_FD_OFFSET) {
		if (!host_write)
			hostcall_init();
		return host_write(fd, buf, count);
	}
	return lkl_sys_write(fd - LKL_FD_OFFSET, (void *)buf, count);
}

ssize_t writev(int fd, const struct iovec *iov, int count)
{
	if (fd < LKL_FD_OFFSET) {
		if (!host_writev)
			hostcall_init();
		return host_writev(fd, iov, count);
	}
	return lkl_sys_writev(fd - LKL_FD_OFFSET, (void *)iov, count);
}

ssize_t sendto(int fd, const void *buf, size_t len, int flags,
			const struct sockaddr *dest_addr, unsigned int addrlen)
{
	if (fd < LKL_FD_OFFSET)
		return host_sendto(fd, buf, len, flags, dest_addr, addrlen);
	return lkl_sys_sendto(fd - LKL_FD_OFFSET, (void *)buf, len,
				      flags, (struct sockaddr *)dest_addr,
				      addrlen);
}

ssize_t send(int fd, const void *buf, size_t len, int flags)
{
	return sendto(fd, (void *)buf, len, flags, NULL, 0);
}

ssize_t read(int fd, void *buf, size_t count)
{
	if (fd < LKL_FD_OFFSET) {
		if (!host_read)
			hostcall_init();
		return host_read(fd, buf, count);
	}
	return lkl_sys_read(fd - LKL_FD_OFFSET, (void *)buf, count);
}

ssize_t recvfrom(int fd, void *buf, size_t len, int flags,
		      struct sockaddr *from, socklen_t *fromlen)
{
	return lkl_sys_recvfrom(fd - LKL_FD_OFFSET, buf, len, flags,
					from, (int *)fromlen);
}

ssize_t recv(int fd, void *buf, size_t count, int flags)
{
	return recvfrom(fd, buf, count, flags, NULL, NULL);
}

int setsockopt(int fd, int level, int optname,
	       const void *optval, socklen_t optlen)
{
	if (fd < LKL_FD_OFFSET)
		return host_setsockopt(fd, level, optname, optval, optlen);
	return lkl_sys_setsockopt(fd - LKL_FD_OFFSET, level, optname,
					  (void *)optval, optlen);
}

int getsockopt(int fd, int level, int optname,
	       void *optval, socklen_t *optlen)
{
	return lkl_sys_getsockopt(fd - LKL_FD_OFFSET, level, optname,
					  optval, (int *)optlen);
}

int ioctl(int fd, unsigned long int request, ...)
{
	va_list vl;
	char *argp;

	va_start(vl, request);
	argp = va_arg(vl, char *);
	va_end(vl);

	if (fd < LKL_FD_OFFSET) {
		if (!host_ioctl)
			hostcall_init();
		return host_ioctl(fd, request, argp);
	}
	return lkl_sys_ioctl(fd - LKL_FD_OFFSET, request,
				     (unsigned long)argp);
}

int fcntl(int fd, int cmd, ... /* arg */)
{
	va_list vl;
	int *argp;

	va_start(vl, cmd);
	argp = va_arg(vl, int *);
	va_end(vl);

	if (fd < LKL_FD_OFFSET) {
		if (!host_fcntl)
			hostcall_init();
		return host_fcntl(fd, cmd, argp);
	}
	return lkl_sys_fcntl(fd - LKL_FD_OFFSET, cmd,
				     (unsigned long)argp);
}

#ifdef notyet
int open(const char *pathname, int flags, ...)
{
	va_list vl;
	int fd;

	va_start(vl, flags);
	if (!host_open)
		hostcall_init();
	fd = host_open(pathname, flags, va_arg(vl, mode_t));
	va_end(vl);
	return fd;
}

int open64(const char *pathname, int flags, mode_t mode)
{
	if (!host_open64)
		hostcall_init();
	int fd = host_open64(pathname, flags, mode);
	return fd;
}
#endif

int pipe(int pipefd[2])
{
	return lkl_sys_pipe(pipefd);
}

int
poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
	struct lkl_pollfd lkl_fds[nfds];
	unsigned int i;

	/* FIXME: need to handle mixed case of hostfd and lklfd. */
	if (fds[0].fd < LKL_FD_OFFSET)
		return host_poll(fds, nfds, timeout);

	for (i = 0; i < nfds; i++)
		lkl_fds[i].fd = fds[i].fd - LKL_FD_OFFSET;

	return lkl_sys_poll(lkl_fds, nfds, timeout);
}

int __poll(struct pollfd *, nfds_t, int) __attribute__((alias("poll")));

int
select(int nfds, fd_set *readfds, fd_set *writefds,
	fd_set *exceptfds, struct timeval *timeout)
{
	/* FIXME: need to handle mixed case of hostfd and lklfd. */
	int fd, host_flag = 0;
	fd_set lkl_rfds[nfds] __attribute__((unused)),
		lkl_wfds[nfds] __attribute__((unused)),
		lkl_efds[nfds] __attribute__((unused));

	for (fd = 0; fd < nfds; fd++) {
		if (fd > LKL_FD_OFFSET)
			break;

		if (readfds != 0 && FD_ISSET(fd, readfds)) {
			host_flag = 1;
			break;
		}
		if (writefds != 0 &&  FD_ISSET(fd, writefds)) {
			host_flag = 1;
			break;
		}
		if (exceptfds != 0 && FD_ISSET(fd, exceptfds)) {
			host_flag = 1;
			break;
		}
	}

	if (host_flag)
		return host_select(nfds, readfds, writefds, exceptfds, timeout);

	return lkl_sys_select(nfds, readfds, writefds,
				      exceptfds, (struct lkl_timeval *)timeout);
}

int
epoll_create(int size)
{
	int ret;

	ret = lkl_sys_epoll_create(size);
	if (ret >= 0)
		return ret + LKL_FD_OFFSET;

	return ret;
}

int
epoll_ctl(int epollfd, int op, int fd, struct epoll_event *event)
{
	return lkl_sys_epoll_ctl(epollfd - LKL_FD_OFFSET, op,
					 fd - LKL_FD_OFFSET, event);
}

int
epoll_wait(int epollfd, struct epoll_event *events,
		int maxevents, int timeout)
{
	/* FIXME: need to handle mixed case of hostfd and lklfd. */
	return lkl_sys_epoll_wait(epollfd - LKL_FD_OFFSET, events,
					  maxevents, timeout);
}

