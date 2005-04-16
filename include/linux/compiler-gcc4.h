/* Never include this file directly.  Include <linux/compiler.h> instead.  */

/* These definitions are for GCC v4.x.  */
#include <linux/compiler-gcc.h>

#define inline			inline		__attribute__((always_inline))
#define __inline__		__inline__	__attribute__((always_inline))
#define __inline		__inline	__attribute__((always_inline))
#define __deprecated		__attribute__((deprecated))
#define __attribute_used__	__attribute__((__used__))
#define __attribute_pure__	__attribute__((pure))
#define __attribute_const__	__attribute__((__const__))
#define  noinline		__attribute__((noinline))
#define __must_check 		__attribute__((warn_unused_result))
#define __compiler_offsetof(a,b) __builtin_offsetof(a,b)

