/*
 *  include/asm-s390/unaligned.h
 *
 *  S390 version
 *
 *  Derived from "include/asm-i386/unaligned.h"
 */

#ifndef __S390_UNALIGNED_H
#define __S390_UNALIGNED_H

/*
 * The S390 can do unaligned accesses itself. 
 *
 * The strange macros are there to make sure these can't
 * be misused in a way that makes them not work on other
 * architectures where unaligned accesses aren't as simple.
 */

#define get_unaligned(ptr) (*(ptr))

#define put_unaligned(val, ptr) ((void)( *(ptr) = (val) ))

#endif
