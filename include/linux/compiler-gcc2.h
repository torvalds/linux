/* Never include this file directly.  Include <linux/compiler.h> instead.  */

/* These definitions are for GCC v2.x.  */

/* Somewhere in the middle of the GCC 2.96 development cycle, we implemented
   a mechanism by which the user can annotate likely branch directions and
   expect the blocks to be reordered appropriately.  Define __builtin_expect
   to nothing for earlier compilers.  */
#include <linux/compiler-gcc.h>

#if __GNUC_MINOR__ < 96
# define __builtin_expect(x, expected_value) (x)
#endif

#define __attribute_used__	__attribute__((__unused__))

/*
 * The attribute `pure' is not implemented in GCC versions earlier
 * than 2.96.
 */
#if __GNUC_MINOR__ >= 96
# define __attribute_pure__	__attribute__((pure))
# define __attribute_const__	__attribute__((__const__))
#endif

/* GCC 2.95.x/2.96 recognize __va_copy, but not va_copy. Actually later GCC's
 * define both va_copy and __va_copy, but the latter may go away, so limit this
 * to this header */
#define va_copy			__va_copy
