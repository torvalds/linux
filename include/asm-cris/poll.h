#ifndef __ASM_CRIS_POLL_H
#define __ASM_CRIS_POLL_H

/* taken from asm-alpha */

#define POLLIN		1
#define POLLPRI		2
#define POLLOUT		4
#define POLLERR		8
#define POLLHUP		16
#define POLLNVAL	32
#define POLLRDNORM	64
#define POLLRDBAND	128
#define POLLWRNORM	256
#define POLLWRBAND	512
#define POLLMSG		1024
#define POLLREMOVE	4096
#define POLLRDHUP       8192

struct pollfd {
	int fd;
	short events;
	short revents;
};

#endif
