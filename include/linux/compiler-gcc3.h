/* Never include this file directly.  Include <linux/compiler.h> instead.  */

/* These definitions are for GCC v3.x.  */
#include <linux/compiler-gcc.h>

#if __GNUC_MINOR__ >= 1
# define inline		inline		__attribute__((always_inline))
# define __inline__	__inline__	__attribute__((always_inline))
# define __inline	__inline	__attribute__((always_inline))
#endif

#if __GNUC_MINOR__ > 0
# define __deprecated		__attribute__((deprecated))
#endif

#if __GNUC_MINOR__ >= 3
# define __attribute_used__	__attribute__((__used__))
#else
# define __attribute_used__	__attribute__((__unused__))
#endif

#define __attribute_pure__	__attribute__((pure))
#define __attribute_const__	__attribute__((__const__))

#if __GNUC_MINOR__ >= 1
#define  noinline		__attribute__((noinline))
#endif

#if __GNUC_MINOR__ >= 4
#define __must_check		__attribute__((warn_unused_result))
#endif

