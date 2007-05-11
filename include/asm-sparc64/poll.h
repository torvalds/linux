#ifndef __SPARC64_POLL_H
#define __SPARC64_POLL_H

#define POLLWRNORM	POLLOUT
#define POLLWRBAND	256
#define POLLMSG		512
#define POLLREMOVE	1024
#define POLLRDHUP       2048

#include <asm-generic/poll.h>

#endif
