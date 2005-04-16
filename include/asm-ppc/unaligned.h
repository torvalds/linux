#ifdef __KERNEL__
#ifndef __PPC_UNALIGNED_H
#define __PPC_UNALIGNED_H

/*
 * The PowerPC can do unaligned accesses itself in big endian mode.
 *
 * The strange macros are there to make sure these can't
 * be misused in a way that makes them not work on other
 * architectures where unaligned accesses aren't as simple.
 */

#define get_unaligned(ptr) (*(ptr))

#define put_unaligned(val, ptr) ((void)( *(ptr) = (val) ))

#endif
#endif /* __KERNEL__ */
