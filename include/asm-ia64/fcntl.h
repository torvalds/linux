#ifndef _ASM_IA64_FCNTL_H
#define _ASM_IA64_FCNTL_H
/*
 * Modified 1998-2000
 *	David Mosberger-Tang <davidm@hpl.hp.com>, Hewlett-Packard Co.
 */

struct flock {
	short l_type;
	short l_whence;
	off_t l_start;
	off_t l_len;
	pid_t l_pid;
};

#define force_o_largefile()	\
		(personality(current->personality) != PER_LINUX32)

#include <asm-generic/fcntl.h>

#endif /* _ASM_IA64_FCNTL_H */
