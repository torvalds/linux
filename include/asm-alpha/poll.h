#ifndef __ALPHA_POLL_H
#define __ALPHA_POLL_H

#define POLLIN		(1 << 0)
#define POLLPRI		(1 << 1)
#define POLLOUT		(1 << 2)
#define POLLERR		(1 << 3)
#define POLLHUP		(1 << 4)
#define POLLNVAL	(1 << 5)
#define POLLRDNORM	(1 << 6)
#define POLLRDBAND	(1 << 7)
#define POLLWRNORM	(1 << 8)
#define POLLWRBAND	(1 << 9)
#define POLLMSG		(1 << 10)
#define POLLREMOVE	(1 << 12)
#define POLLRDHUP       (1 << 13)


struct pollfd {
	int fd;
	short events;
	short revents;
};

#endif
