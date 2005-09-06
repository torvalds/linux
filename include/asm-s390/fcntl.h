/*
 *  include/asm-s390/fcntl.h
 *
 *  S390 version
 *
 *  Derived from "include/asm-i386/fcntl.h"
 */
#ifndef _S390_FCNTL_H
#define _S390_FCNTL_H

#ifndef __s390x__
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
#endif

#include <asm-generic/fcntl.h>

#endif
