/*
 *  include/asm-s390/types.h
 *
 *  S390 version
 *
 *  Derived from "include/asm-i386/types.h"
 */

#ifndef _S390_TYPES_H
#define _S390_TYPES_H

#ifndef __s390x__
# include <asm-generic/int-ll64.h>
#else
# include <asm-generic/int-l64.h>
#endif

#ifndef __ASSEMBLY__

typedef unsigned short umode_t;

/* A address type so that arithmetic can be done on it & it can be upgraded to
   64 bit when necessary 
*/
typedef unsigned long addr_t; 
typedef __signed__ long saddr_t;

#endif /* __ASSEMBLY__ */

/*
 * These aren't exported outside the kernel to avoid name space clashes
 */
#ifdef __KERNEL__

#ifndef __s390x__
#define BITS_PER_LONG 32
#else
#define BITS_PER_LONG 64
#endif

#ifndef __ASSEMBLY__

typedef u32 dma_addr_t;

#ifndef __s390x__
typedef union {
	unsigned long long pair;
	struct {
		unsigned long even;
		unsigned long odd;
	} subreg;
} register_pair;

#endif /* ! __s390x__   */
#endif /* __ASSEMBLY__  */
#endif /* __KERNEL__    */
#endif /* _S390_TYPES_H */
