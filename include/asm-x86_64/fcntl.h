#ifndef _X86_64_FCNTL_H
#define _X86_64_FCNTL_H

struct flock {
	short  l_type;
	short  l_whence;
	off_t l_start;
	off_t l_len;
	pid_t  l_pid;
};

#include <asm-generic/fcntl.h>

#endif /* !_X86_64_FCNTL_H */
