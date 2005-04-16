/*
 *  include/asm-s390/siginfo.h
 *
 *  S390 version
 *
 *  Derived from "include/asm-i386/siginfo.h"
 */

#ifndef _S390_SIGINFO_H
#define _S390_SIGINFO_H

#ifdef __s390x__
#define __ARCH_SI_PREAMBLE_SIZE (4 * sizeof(int))
#endif

#ifdef CONFIG_ARCH_S390X
#define SIGEV_PAD_SIZE ((SIGEV_MAX_SIZE/sizeof(int)) - 4)
#else
#define SIGEV_PAD_SIZE ((SIGEV_MAX_SIZE/sizeof(int)) - 3)
#endif

#include <asm-generic/siginfo.h>

#endif
