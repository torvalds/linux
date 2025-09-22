//===-- asan_interface_internal.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// This header declares the AddressSanitizer runtime interface functions.
// The runtime library has to define these functions so the instrumented program
// could call them.
//
// See also include/sanitizer/asan_interface.h
//===----------------------------------------------------------------------===//
#ifndef ASAN_INTERFACE_INTERNAL_H
#define ASAN_INTERFACE_INTERNAL_H

#include "sanitizer_common/sanitizer_internal_defs.h"

#include "asan_init_version.h"

using __sanitizer::uptr;
using __sanitizer::u64;
using __sanitizer::u32;

extern "C" {
  // This function should be called at the very beginning of the process,
  // before any instrumented code is executed and before any call to malloc.
  SANITIZER_INTERFACE_ATTRIBUTE void __asan_init();

  // This function exists purely to get a linker/loader error when using
  // incompatible versions of instrumentation and runtime library. Please note
  // that __asan_version_mismatch_check is a macro that is replaced with
  // __asan_version_mismatch_check_vXXX at compile-time.
  SANITIZER_INTERFACE_ATTRIBUTE void __asan_version_mismatch_check();

  // This structure is used to describe the source location of a place where
  // global was defined.
  struct __asan_global_source_location {
    const char *filename;
    int line_no;
    int column_no;
  };

  // This structure describes an instrumented global variable.
  struct __asan_global {
    uptr beg;                // The address of the global.
    uptr size;               // The original size of the global.
    uptr size_with_redzone;  // The size with the redzone.
    const char *name;        // Name as a C string.
    const char *module_name; // Module name as a C string. This pointer is a
                             // unique identifier of a module.
    uptr has_dynamic_init;   // Non-zero if the global has dynamic initializer.
    __asan_global_source_location *gcc_location;  // Source location of a global,
                                                  // used by GCC compiler. LLVM uses
                                                  // llvm-symbolizer that relies
                                                  // on DWARF debugging info.
    uptr odr_indicator;      // The address of the ODR indicator symbol.
  };

  // These functions can be called on some platforms to find globals in the same
  // loaded image as `flag' and apply __asan_(un)register_globals to them,
  // filtering out redundant calls.
  SANITIZER_INTERFACE_ATTRIBUTE
  void __asan_register_image_globals(uptr *flag);
  SANITIZER_INTERFACE_ATTRIBUTE
  void __asan_unregister_image_globals(uptr *flag);

  SANITIZER_INTERFACE_ATTRIBUTE
  void __asan_register_elf_globals(uptr *flag, void *start, void *stop);
  SANITIZER_INTERFACE_ATTRIBUTE
  void __asan_unregister_elf_globals(uptr *flag, void *start, void *stop);

  // These two functions should be called by the instrumented code.
  // 'globals' is an array of structures describing 'n' globals.
  SANITIZER_INTERFACE_ATTRIBUTE
  void __asan_register_globals(__asan_global *globals, uptr n);
  SANITIZER_INTERFACE_ATTRIBUTE
  void __asan_unregister_globals(__asan_global *globals, uptr n);

  // These two functions should be called before and after dynamic initializers
  // of a single module run, respectively.
  SANITIZER_INTERFACE_ATTRIBUTE
  void __asan_before_dynamic_init(const char *module_name);
  SANITIZER_INTERFACE_ATTRIBUTE
  void __asan_after_dynamic_init();

  // Sets bytes of the given range of the shadow memory into specific value.
  SANITIZER_INTERFACE_ATTRIBUTE
  void __asan_set_shadow_00(uptr addr, uptr size);
  SANITIZER_INTERFACE_ATTRIBUTE
  void __asan_set_shadow_01(uptr addr, uptr size);
  SANITIZER_INTERFACE_ATTRIBUTE
  void __asan_set_shadow_02(uptr addr, uptr size);
  SANITIZER_INTERFACE_ATTRIBUTE
  void __asan_set_shadow_03(uptr addr, uptr size);
  SANITIZER_INTERFACE_ATTRIBUTE
  void __asan_set_shadow_04(uptr addr, uptr size);
  SANITIZER_INTERFACE_ATTRIBUTE
  void __asan_set_shadow_05(uptr addr, uptr size);
  SANITIZER_INTERFACE_ATTRIBUTE
  void __asan_set_shadow_06(uptr addr, uptr size);
  SANITIZER_INTERFACE_ATTRIBUTE
  void __asan_set_shadow_07(uptr addr, uptr size);
  SANITIZER_INTERFACE_ATTRIBUTE
  void __asan_set_shadow_f1(uptr addr, uptr size);
  SANITIZER_INTERFACE_ATTRIBUTE
  void __asan_set_shadow_f2(uptr addr, uptr size);
  SANITIZER_INTERFACE_ATTRIBUTE
  void __asan_set_shadow_f3(uptr addr, uptr size);
  SANITIZER_INTERFACE_ATTRIBUTE
  void __asan_set_shadow_f5(uptr addr, uptr size);
  SANITIZER_INTERFACE_ATTRIBUTE
  void __asan_set_shadow_f8(uptr addr, uptr size);

  // These two functions are used by instrumented code in the
  // use-after-scope mode. They mark memory for local variables as
  // unaddressable when they leave scope and addressable before the
  // function exits.
  SANITIZER_INTERFACE_ATTRIBUTE
  void __asan_poison_stack_memory(uptr addr, uptr size);
  SANITIZER_INTERFACE_ATTRIBUTE
  void __asan_unpoison_stack_memory(uptr addr, uptr size);

  // Performs cleanup before a NoReturn function. Must be called before things
  // like _exit and execl to avoid false positives on stack.
  SANITIZER_INTERFACE_ATTRIBUTE void __asan_handle_no_return();

  SANITIZER_INTERFACE_ATTRIBUTE
  void __asan_poison_memory_region(void const volatile *addr, uptr size);
  SANITIZER_INTERFACE_ATTRIBUTE
  void __asan_unpoison_memory_region(void const volatile *addr, uptr size);

  SANITIZER_INTERFACE_ATTRIBUTE
  int __asan_address_is_poisoned(void const volatile *addr);

  SANITIZER_INTERFACE_ATTRIBUTE
  uptr __asan_region_is_poisoned(uptr beg, uptr size);

  SANITIZER_INTERFACE_ATTRIBUTE
  void __asan_describe_address(uptr addr);

  SANITIZER_INTERFACE_ATTRIBUTE
  int __asan_report_present();

  SANITIZER_INTERFACE_ATTRIBUTE
  uptr __asan_get_report_pc();
  SANITIZER_INTERFACE_ATTRIBUTE
  uptr __asan_get_report_bp();
  SANITIZER_INTERFACE_ATTRIBUTE
  uptr __asan_get_report_sp();
  SANITIZER_INTERFACE_ATTRIBUTE
  uptr __asan_get_report_address();
  SANITIZER_INTERFACE_ATTRIBUTE
  int __asan_get_report_access_type();
  SANITIZER_INTERFACE_ATTRIBUTE
  uptr __asan_get_report_access_size();
  SANITIZER_INTERFACE_ATTRIBUTE
  const char * __asan_get_report_description();

  SANITIZER_INTERFACE_ATTRIBUTE
  const char * __asan_locate_address(uptr addr, char *name, uptr name_size,
                                     uptr *region_address, uptr *region_size);

  SANITIZER_INTERFACE_ATTRIBUTE
  uptr __asan_get_alloc_stack(uptr addr, uptr *trace, uptr size,
                              u32 *thread_id);

  SANITIZER_INTERFACE_ATTRIBUTE
  uptr __asan_get_free_stack(uptr addr, uptr *trace, uptr size,
                             u32 *thread_id);

  SANITIZER_INTERFACE_ATTRIBUTE
  void __asan_get_shadow_mapping(uptr *shadow_scale, uptr *shadow_offset);

  SANITIZER_INTERFACE_ATTRIBUTE
  void __asan_report_error(uptr pc, uptr bp, uptr sp,
                           uptr addr, int is_write, uptr access_size, u32 exp);

  SANITIZER_INTERFACE_ATTRIBUTE
  void __asan_set_death_callback(void (*callback)(void));
  SANITIZER_INTERFACE_ATTRIBUTE
  void __asan_set_error_report_callback(void (*callback)(const char*));

  SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE
  void __asan_on_error();

  SANITIZER_INTERFACE_ATTRIBUTE void __asan_print_accumulated_stats();

  SANITIZER_INTERFACE_ATTRIBUTE
  const char *__asan_default_options();

  SANITIZER_INTERFACE_ATTRIBUTE
  extern uptr __asan_shadow_memory_dynamic_address;

  // Global flag, copy of ASAN_OPTIONS=detect_stack_use_after_return
  SANITIZER_INTERFACE_ATTRIBUTE
  extern int __asan_option_detect_stack_use_after_return;

  SANITIZER_INTERFACE_ATTRIBUTE
  extern uptr *__asan_test_only_reported_buggy_pointer;

  SANITIZER_INTERFACE_ATTRIBUTE void __asan_load1(uptr p);
  SANITIZER_INTERFACE_ATTRIBUTE void __asan_load2(uptr p);
  SANITIZER_INTERFACE_ATTRIBUTE void __asan_load4(uptr p);
  SANITIZER_INTERFACE_ATTRIBUTE void __asan_load8(uptr p);
  SANITIZER_INTERFACE_ATTRIBUTE void __asan_load16(uptr p);
  SANITIZER_INTERFACE_ATTRIBUTE void __asan_store1(uptr p);
  SANITIZER_INTERFACE_ATTRIBUTE void __asan_store2(uptr p);
  SANITIZER_INTERFACE_ATTRIBUTE void __asan_store4(uptr p);
  SANITIZER_INTERFACE_ATTRIBUTE void __asan_store8(uptr p);
  SANITIZER_INTERFACE_ATTRIBUTE void __asan_store16(uptr p);
  SANITIZER_INTERFACE_ATTRIBUTE void __asan_loadN(uptr p, uptr size);
  SANITIZER_INTERFACE_ATTRIBUTE void __asan_storeN(uptr p, uptr size);

  SANITIZER_INTERFACE_ATTRIBUTE void __asan_load1_noabort(uptr p);
  SANITIZER_INTERFACE_ATTRIBUTE void __asan_load2_noabort(uptr p);
  SANITIZER_INTERFACE_ATTRIBUTE void __asan_load4_noabort(uptr p);
  SANITIZER_INTERFACE_ATTRIBUTE void __asan_load8_noabort(uptr p);
  SANITIZER_INTERFACE_ATTRIBUTE void __asan_load16_noabort(uptr p);
  SANITIZER_INTERFACE_ATTRIBUTE void __asan_store1_noabort(uptr p);
  SANITIZER_INTERFACE_ATTRIBUTE void __asan_store2_noabort(uptr p);
  SANITIZER_INTERFACE_ATTRIBUTE void __asan_store4_noabort(uptr p);
  SANITIZER_INTERFACE_ATTRIBUTE void __asan_store8_noabort(uptr p);
  SANITIZER_INTERFACE_ATTRIBUTE void __asan_store16_noabort(uptr p);
  SANITIZER_INTERFACE_ATTRIBUTE void __asan_loadN_noabort(uptr p, uptr size);
  SANITIZER_INTERFACE_ATTRIBUTE void __asan_storeN_noabort(uptr p, uptr size);

  SANITIZER_INTERFACE_ATTRIBUTE void __asan_exp_load1(uptr p, u32 exp);
  SANITIZER_INTERFACE_ATTRIBUTE void __asan_exp_load2(uptr p, u32 exp);
  SANITIZER_INTERFACE_ATTRIBUTE void __asan_exp_load4(uptr p, u32 exp);
  SANITIZER_INTERFACE_ATTRIBUTE void __asan_exp_load8(uptr p, u32 exp);
  SANITIZER_INTERFACE_ATTRIBUTE void __asan_exp_load16(uptr p, u32 exp);
  SANITIZER_INTERFACE_ATTRIBUTE void __asan_exp_store1(uptr p, u32 exp);
  SANITIZER_INTERFACE_ATTRIBUTE void __asan_exp_store2(uptr p, u32 exp);
  SANITIZER_INTERFACE_ATTRIBUTE void __asan_exp_store4(uptr p, u32 exp);
  SANITIZER_INTERFACE_ATTRIBUTE void __asan_exp_store8(uptr p, u32 exp);
  SANITIZER_INTERFACE_ATTRIBUTE void __asan_exp_store16(uptr p, u32 exp);
  SANITIZER_INTERFACE_ATTRIBUTE void __asan_exp_loadN(uptr p, uptr size,
                                                      u32 exp);
  SANITIZER_INTERFACE_ATTRIBUTE void __asan_exp_storeN(uptr p, uptr size,
                                                       u32 exp);

  SANITIZER_INTERFACE_ATTRIBUTE
      void* __asan_memcpy(void *dst, const void *src, uptr size);
  SANITIZER_INTERFACE_ATTRIBUTE
      void* __asan_memset(void *s, int c, uptr n);
  SANITIZER_INTERFACE_ATTRIBUTE
      void* __asan_memmove(void* dest, const void* src, uptr n);

  SANITIZER_INTERFACE_ATTRIBUTE
  void __asan_poison_cxx_array_cookie(uptr p);
  SANITIZER_INTERFACE_ATTRIBUTE
  uptr __asan_load_cxx_array_cookie(uptr *p);
  SANITIZER_INTERFACE_ATTRIBUTE
  void __asan_poison_intra_object_redzone(uptr p, uptr size);
  SANITIZER_INTERFACE_ATTRIBUTE
  void __asan_unpoison_intra_object_redzone(uptr p, uptr size);
  SANITIZER_INTERFACE_ATTRIBUTE
  void __asan_alloca_poison(uptr addr, uptr size);
  SANITIZER_INTERFACE_ATTRIBUTE
  void __asan_allocas_unpoison(uptr top, uptr bottom);

  SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE
  const char* __asan_default_suppressions();

  SANITIZER_INTERFACE_ATTRIBUTE void __asan_handle_vfork(void *sp);

  SANITIZER_INTERFACE_ATTRIBUTE int __asan_update_allocation_context(
      void *addr);
}  // extern "C"

#endif  // ASAN_INTERFACE_INTERNAL_H
