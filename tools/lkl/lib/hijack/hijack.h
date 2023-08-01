/* SPDX-License-Identifier: GPL-2.0 */

#include <sys/socket.h>
#include <sys/epoll.h>
#include <poll.h>


int is_lklfd(int fd);
int hijack_setsockopt(int fd, int level, int optname, const void *optval,
		      socklen_t optlen);
int hijack_getsockopt(int fd, int level, int optname, void *optval, socklen_t *optlen);
int hijack_poll(struct pollfd *fds, nfds_t nfds, int timeout);
int hijack_select(int nfds, fd_set *r, fd_set *w, fd_set *e, struct timeval *t);
int hijack_eventfd(unsigned int count, int flags);
int hijack_epoll_create(int size);
int hijack_epoll_create1(int flags);
int hijack_epoll_ctl(int epollfd, int op, int fd, struct epoll_event *event);
int hijack_epoll_wait(int epfd, struct epoll_event *events,
	      int maxevents, int timeout);
int hijack_eventfd_read(int fd, uint64_t *value);
int hijack_eventfd_write(int fd, uint64_t value);
