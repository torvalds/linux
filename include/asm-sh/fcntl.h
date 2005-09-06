#ifndef __ASM_SH_FCNTL_H
#define __ASM_SH_FCNTL_H

#define F_GETLK64	12	/*  using 'struct flock64' */
#define F_SETLK64	13
#define F_SETLKW64	14

struct flock64 {
	short  l_type;
	short  l_whence;
	loff_t l_start;
	loff_t l_len;
	pid_t  l_pid;
};

#include <asm-generic/fcntl.h>

#endif /* __ASM_SH_FCNTL_H */

