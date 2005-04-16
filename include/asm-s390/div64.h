#ifndef __S390_DIV64
#define __S390_DIV64

#ifndef __s390x__

/* for do_div "base" needs to be smaller than 2^31-1 */
#define do_div(n, base) ({                                      \
	unsigned long long __n = (n);				\
	unsigned long __r;					\
								\
	asm ("   slr  0,0\n"					\
	     "   l    1,%1\n"					\
	     "   srdl 0,1\n"					\
	     "   dr   0,%2\n"					\
	     "   alr  1,1\n"					\
	     "   alr  0,0\n"					\
	     "   lhi  2,1\n"					\
	     "   n    2,%1\n"					\
	     "   alr  0,2\n"					\
	     "   clr  0,%2\n"					\
	     "   jl   0f\n"					\
	     "   slr  0,%2\n"					\
             "   ahi  1,1\n"					\
	     "0: st   1,%1\n"					\
	     "   l    1,4+%1\n"					\
	     "   srdl 0,1\n"					\
             "   dr   0,%2\n"					\
	     "   alr  1,1\n"					\
	     "   alr  0,0\n"					\
	     "   lhi  2,1\n"					\
	     "   n    2,4+%1\n"					\
	     "   alr  0,2\n"					\
	     "   clr  0,%2\n"					\
             "   jl   1f\n"					\
	     "   slr  0,%2\n"					\
	     "   ahi  1,1\n"					\
	     "1: st   1,4+%1\n"					\
             "   lr   %0,0"					\
	     : "=d" (__r), "=m" (__n)				\
	     : "d" (base), "m" (__n) : "0", "1", "2", "cc" );	\
	(n) = (__n);						\
        __r;                                                    \
})

#else /* __s390x__ */
#include <asm-generic/div64.h>
#endif /* __s390x__ */

#endif
