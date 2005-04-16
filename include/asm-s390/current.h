/*
 *  include/asm-s390/current.h
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  Derived from "include/asm-i386/current.h"
 */

#ifndef _S390_CURRENT_H
#define _S390_CURRENT_H

#ifdef __KERNEL__
#include <asm/lowcore.h>

struct task_struct;

#define current ((struct task_struct *const)S390_lowcore.current_task)

#endif

#endif /* !(_S390_CURRENT_H) */
