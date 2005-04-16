#ifndef __X8664_UNALIGNED_H
#define __X8664_UNALIGNED_H

/*
 * The x86-64 can do unaligned accesses itself. 
 *
 * The strange macros are there to make sure these can't
 * be misused in a way that makes them not work on other
 * architectures where unaligned accesses aren't as simple.
 */

/**
 * get_unaligned - get value from possibly mis-aligned location
 * @ptr: pointer to value
 *
 * This macro should be used for accessing values larger in size than 
 * single bytes at locations that are expected to be improperly aligned, 
 * e.g. retrieving a u16 value from a location not u16-aligned.
 *
 * Note that unaligned accesses can be very expensive on some architectures.
 */
#define get_unaligned(ptr) (*(ptr))

/**
 * put_unaligned - put value to a possibly mis-aligned location
 * @val: value to place
 * @ptr: pointer to location
 *
 * This macro should be used for placing values larger in size than 
 * single bytes at locations that are expected to be improperly aligned, 
 * e.g. writing a u16 value to a location not u16-aligned.
 *
 * Note that unaligned accesses can be very expensive on some architectures.
 */
#define put_unaligned(val, ptr) ((void)( *(ptr) = (val) ))

#endif
