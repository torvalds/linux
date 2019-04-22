/*
 * Will go away once libc support is there
 */
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <signal.h>
#include "liburing.h"

#if defined(__x86_64) || defined(__i386__)
#ifndef __NR_sys_io_uring_setup
#define __NR_sys_io_uring_setup		425
#endif
#ifndef __NR_sys_io_uring_enter
#define __NR_sys_io_uring_enter		426
#endif
#ifndef __NR_sys_io_uring_register
#define __NR_sys_io_uring_register	427
#endif
#else
#error "Arch not supported yet"
#endif

int io_uring_register(int fd, unsigned int opcode, void *arg,
		      unsigned int nr_args)
{
	return syscall(__NR_sys_io_uring_register, fd, opcode, arg, nr_args);
}

int io_uring_setup(unsigned entries, struct io_uring_params *p)
{
	return syscall(__NR_sys_io_uring_setup, entries, p);
}

int io_uring_enter(unsigned fd, unsigned to_submit, unsigned min_complete,
		   unsigned flags, sigset_t *sig)
{
	return syscall(__NR_sys_io_uring_enter, fd, to_submit, min_complete,
			flags, sig, _NSIG / 8);
}
