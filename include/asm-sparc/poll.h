#ifndef __SPARC_POLL_H
#define __SPARC_POLL_H

#define POLLIN		  1
#define POLLPRI		  2
#define POLLOUT		  4
#define POLLERR		  8
#define POLLHUP		 16
#define POLLNVAL	 32
#define POLLRDNORM	 64
#define POLLWRNORM	POLLOUT
#define POLLRDBAND	128
#define POLLWRBAND	256
#define POLLMSG		512
#define POLLREMOVE	1024

struct pollfd {
	int fd;
	short events;
	short revents;
};

#endif
