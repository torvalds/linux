/* 
This is a clang style test case for checking that preprocessor
defines match gcc.
*/

/*
RUN: for arch in -m32 -m64; do \
RUN:   for lang in -std=gnu89 -ansi -std=c99 -std=gnu99; do \
RUN:     for input in c objective-c; do \
RUN:       for opts in "-O0" "-O1 -dynamic" "-O2 -static" "-Os"; do     \
RUN:         echo "-- $arch, $lang, $input, $opts --"; \
RUN:         for cc in 0 1; do \
RUN:           if [ "$cc" == 0 ]; then \
RUN:             cc_prog=clang; \
RUN:             output=%t0; \
RUN:           else \
RUN:             cc_prog=gcc; \
RUN:             output=%t1; \
RUN:           fi; \
RUN:           $cc_prog $arch $lang $opts -march=core2 -dM -E -x $input %s | sort > $output; \
RUN:          done; \
RUN:          if (! diff %t0 %t1); then exit 1; fi; \
RUN:       done; \
RUN:     done; \
RUN:   done; \
RUN: done;
*/

/* We don't care about this difference */
#ifdef __PIC__
#if __PIC__ == 1
#undef __PIC__
#undef __pic__
#define __PIC__ 2
#define __pic__ 2
#endif
#endif

/* Undefine things we don't expect to match. */
#undef __core2
#undef __core2__
#undef __SSSE3__

/* Undefine things we don't expect to match. */
#undef __DEC_EVAL_METHOD__
#undef __INT16_TYPE__
#undef __INT32_TYPE__
#undef __INT64_TYPE__
#undef __INT8_TYPE__
#undef __SSP__
#undef __APPLE_CC__
#undef __VERSION__
#undef __clang__
#undef __llvm__
#undef __nocona
#undef __nocona__
#undef __k8
#undef __k8__
#undef __tune_nocona__
#undef __tune_core2__
#undef __POINTER_WIDTH__
#undef __INTPTR_TYPE__
#undef __NO_MATH_INLINES

#undef __DEC128_DEN__
#undef __DEC128_EPSILON__
#undef __DEC128_MANT_DIG__
#undef __DEC128_MAX_EXP__
#undef __DEC128_MAX__
#undef __DEC128_MIN_EXP__
#undef __DEC128_MIN__
#undef __DEC32_DEN__
#undef __DEC32_EPSILON__
#undef __DEC32_MANT_DIG__
#undef __DEC32_MAX_EXP__
#undef __DEC32_MAX__
#undef __DEC32_MIN_EXP__
#undef __DEC32_MIN__
#undef __DEC64_DEN__
#undef __DEC64_EPSILON__
#undef __DEC64_MANT_DIG__
#undef __DEC64_MAX_EXP__
#undef __DEC64_MAX__
#undef __DEC64_MIN_EXP__
#undef __DEC64_MIN__
