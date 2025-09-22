//===-- crtbegin.c - Start of constructors and destructors ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <stddef.h>

__attribute__((visibility("hidden"))) void *__dso_handle = &__dso_handle;

#ifdef EH_USE_FRAME_REGISTRY
__extension__ static void *__EH_FRAME_LIST__[]
    __attribute__((section(".eh_frame"), aligned(sizeof(void *)))) = {};

extern void __register_frame_info(const void *, void *) __attribute__((weak));
extern void *__deregister_frame_info(const void *) __attribute__((weak));
#endif

#ifndef CRT_HAS_INITFINI_ARRAY
typedef void (*fp)(void);

static fp __CTOR_LIST__[]
    __attribute__((section(".ctors"), aligned(sizeof(fp)))) = {(fp)-1};
extern fp __CTOR_LIST_END__[];
#endif

extern void __cxa_finalize(void *) __attribute__((weak));

static void __attribute__((used)) __do_init(void) {
  static _Bool __initialized;
  if (__builtin_expect(__initialized, 0))
    return;
  __initialized = 1;

#ifdef EH_USE_FRAME_REGISTRY
  static struct { void *p[8]; } __object;
  if (__register_frame_info)
    __register_frame_info(__EH_FRAME_LIST__, &__object);
#endif
#ifndef CRT_HAS_INITFINI_ARRAY
  const size_t n = __CTOR_LIST_END__ - __CTOR_LIST__ - 1;
  for (size_t i = n; i >= 1; i--) __CTOR_LIST__[i]();
#endif
}

#ifdef CRT_HAS_INITFINI_ARRAY
__attribute__((section(".init_array"),
               used)) static void (*__init)(void) = __do_init;
#elif defined(__i386__) || defined(__x86_64__)
__asm__(".pushsection .init,\"ax\",@progbits\n\t"
        "call __do_init\n\t"
        ".popsection");
#elif defined(__riscv)
__asm__(".pushsection .init,\"ax\",%progbits\n\t"
        "call __do_init\n\t"
        ".popsection");
#elif defined(__arm__) || defined(__aarch64__)
__asm__(".pushsection .init,\"ax\",%progbits\n\t"
        "bl __do_init\n\t"
        ".popsection");
#elif defined(__mips__)
__asm__(".pushsection .init,\"ax\",@progbits\n\t"
        "jal __do_init\n\t"
        ".popsection");
#elif defined(__powerpc__) || defined(__powerpc64__)
__asm__(".pushsection .init,\"ax\",@progbits\n\t"
        "bl __do_init\n\t"
        "nop\n\t"
        ".popsection");
#elif defined(__sparc__)
__asm__(".pushsection .init,\"ax\",@progbits\n\t"
        "call __do_init\n\t"
        ".popsection");
#else
#error "crtbegin without .init_fini array unimplemented for this architecture"
#endif // CRT_HAS_INITFINI_ARRAY

#ifndef CRT_HAS_INITFINI_ARRAY
static fp __DTOR_LIST__[]
    __attribute__((section(".dtors"), aligned(sizeof(fp)))) = {(fp)-1};
extern fp __DTOR_LIST_END__[];
#endif

static void __attribute__((used)) __do_fini(void) {
  static _Bool __finalized;
  if (__builtin_expect(__finalized, 0))
    return;
  __finalized = 1;

  if (__cxa_finalize)
    __cxa_finalize(__dso_handle);

#ifndef CRT_HAS_INITFINI_ARRAY
  const size_t n = __DTOR_LIST_END__ - __DTOR_LIST__ - 1;
  for (size_t i = 1; i <= n; i++) __DTOR_LIST__[i]();
#endif
#ifdef EH_USE_FRAME_REGISTRY
  if (__deregister_frame_info)
    __deregister_frame_info(__EH_FRAME_LIST__);
#endif
}

#ifdef CRT_HAS_INITFINI_ARRAY
__attribute__((section(".fini_array"),
               used)) static void (*__fini)(void) = __do_fini;
#elif defined(__i386__) || defined(__x86_64__)
__asm__(".pushsection .fini,\"ax\",@progbits\n\t"
        "call __do_fini\n\t"
        ".popsection");
#elif defined(__arm__) || defined(__aarch64__)
__asm__(".pushsection .fini,\"ax\",%progbits\n\t"
        "bl __do_fini\n\t"
        ".popsection");
#elif defined(__mips__)
__asm__(".pushsection .fini,\"ax\",@progbits\n\t"
        "jal __do_fini\n\t"
        ".popsection");
#elif defined(__powerpc__) || defined(__powerpc64__)
__asm__(".pushsection .fini,\"ax\",@progbits\n\t"
        "bl __do_fini\n\t"
        "nop\n\t"
        ".popsection");
#elif defined(__riscv)
__asm__(".pushsection .fini,\"ax\",@progbits\n\t"
        "call __do_fini\n\t"
        ".popsection");
#elif defined(__sparc__)
__asm__(".pushsection .fini,\"ax\",@progbits\n\t"
        "call __do_fini\n\t"
        ".popsection");
#else
#error "crtbegin without .init_fini array unimplemented for this architecture"
#endif  // CRT_HAS_INIT_FINI_ARRAY
