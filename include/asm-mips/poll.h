#ifndef __ASM_POLL_H
#define __ASM_POLL_H

#define POLLIN		0x0001
#define POLLPRI		0x0002
#define POLLOUT		0x0004

#define POLLERR		0x0008
#define POLLHUP		0x0010
#define POLLNVAL	0x0020

#define POLLRDNORM	0x0040
#define POLLRDBAND	0x0080
#define POLLWRNORM	POLLOUT
#define POLLWRBAND	0x0100

/* These seem to be more or less nonstandard ...  */
#define POLLMSG		0x0400
#define POLLREMOVE	0x1000

struct pollfd {
	int fd;
	short events;
	short revents;
};

#endif /* __ASM_POLL_H */
