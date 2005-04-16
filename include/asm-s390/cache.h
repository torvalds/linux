/*
 *  include/asm-s390/cache.h
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *
 *  Derived from "include/asm-i386/cache.h"
 *    Copyright (C) 1992, Linus Torvalds
 */

#ifndef __ARCH_S390_CACHE_H
#define __ARCH_S390_CACHE_H

#define L1_CACHE_BYTES     256
#define L1_CACHE_SHIFT     8
#define L1_CACHE_SHIFT_MAX 8	/* largest L1 which this arch supports */

#define ARCH_KMALLOC_MINALIGN	8

#endif
