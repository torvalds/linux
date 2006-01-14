/* Never include this file directly.  Include <linux/compiler.h> instead.  */

/* These definitions are for GCC v4.x.  */
#include <linux/compiler-gcc.h>

#define __attribute_used__	__attribute__((__used__))
#define __must_check 		__attribute__((warn_unused_result))
#define __compiler_offsetof(a,b) __builtin_offsetof(a,b)
#define __always_inline		inline __attribute__((always_inline))
