#ifndef __M68K_UNALIGNED_H
#define __M68K_UNALIGNED_H

#include <linux/config.h>

#ifdef CONFIG_COLDFIRE

#include <asm-generic/unaligned.h>

#else
/*
 * The m68k can do unaligned accesses itself. 
 *
 * The strange macros are there to make sure these can't
 * be misused in a way that makes them not work on other
 * architectures where unaligned accesses aren't as simple.
 */

#define get_unaligned(ptr) (*(ptr))
#define put_unaligned(val, ptr) ((void)( *(ptr) = (val) ))

#endif

#endif
