/* Never include this file directly.  Include <linux/compiler.h> instead.  */

#ifdef __ECC

/* Some compiler specific definitions are overwritten here
 * for Intel ECC compiler
 */

#include <asm/intrinsics.h>

/* Intel ECC compiler doesn't support gcc specific asm stmts.
 * It uses intrinsics to do the equivalent things.
 */
#undef barrier
#undef RELOC_HIDE

#define barrier() __memory_barrier()

#define RELOC_HIDE(ptr, off)					\
  ({ unsigned long __ptr;					\
     __ptr = (unsigned long) (ptr);				\
    (typeof(ptr)) (__ptr + (off)); })

#endif

#define uninitialized_var(x) x
