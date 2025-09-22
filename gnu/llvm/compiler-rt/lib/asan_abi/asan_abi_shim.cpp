//===-asan_abi_shim.cpp - ASan Stable ABI Shim-----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "../asan/asan_interface_internal.h"
#include "asan_abi.h"
#include <assert.h>

extern "C" {
// Functions concerning instrumented global variables
void __asan_register_image_globals(uptr *flag) {
  __asan_abi_register_image_globals();
}
void __asan_unregister_image_globals(uptr *flag) {
  __asan_abi_unregister_image_globals();
}
void __asan_register_elf_globals(uptr *flag, void *start, void *stop) {
  bool bFlag = *flag;
  __asan_abi_register_elf_globals(&bFlag, start, stop);
  *flag = bFlag;
}
void __asan_unregister_elf_globals(uptr *flag, void *start, void *stop) {
  bool bFlag = *flag;
  __asan_abi_unregister_elf_globals(&bFlag, start, stop);
  *flag = bFlag;
}
void __asan_register_globals(__asan_global *globals, uptr n) {
  __asan_abi_register_globals(globals, n);
}
void __asan_unregister_globals(__asan_global *globals, uptr n) {
  __asan_abi_unregister_globals(globals, n);
}

// Functions concerning dynamic library initialization
void __asan_before_dynamic_init(const char *module_name) {
  __asan_abi_before_dynamic_init(module_name);
}
void __asan_after_dynamic_init(void) { __asan_abi_after_dynamic_init(); }

// Functions concerning block memory destinations
void *__asan_memcpy(void *dst, const void *src, uptr size) {
  return __asan_abi_memcpy(dst, src, size);
}
void *__asan_memset(void *s, int c, uptr n) {
  return __asan_abi_memset(s, c, n);
}
void *__asan_memmove(void *dest, const void *src, uptr n) {
  return __asan_abi_memmove(dest, src, n);
}

// Functions concerning RTL startup and initialization
void __asan_init(void) {
  static_assert(sizeof(uptr) == 8 || sizeof(uptr) == 4);
  static_assert(sizeof(u64) == 8);
  static_assert(sizeof(u32) == 4);

  __asan_abi_init();
}

void __asan_handle_no_return(void) { __asan_abi_handle_no_return(); }

// Variables concerning RTL state. These provisionally exist for completeness
// but will likely move into the Stable ABI implementation and not in the shim.
uptr __asan_shadow_memory_dynamic_address = (uptr)0L;
int __asan_option_detect_stack_use_after_return = 1;

// Functions concerning memory load and store reporting
void __asan_report_load1(uptr addr) {
  __asan_abi_report_load_n((void *)addr, 1, true);
}
void __asan_report_load2(uptr addr) {
  __asan_abi_report_load_n((void *)addr, 2, true);
}
void __asan_report_load4(uptr addr) {
  __asan_abi_report_load_n((void *)addr, 4, true);
}
void __asan_report_load8(uptr addr) {
  __asan_abi_report_load_n((void *)addr, 8, true);
}
void __asan_report_load16(uptr addr) {
  __asan_abi_report_load_n((void *)addr, 16, true);
}
void __asan_report_load_n(uptr addr, uptr size) {
  __asan_abi_report_load_n((void *)addr, size, true);
}
void __asan_report_store1(uptr addr) {
  __asan_abi_report_store_n((void *)addr, 1, true);
}
void __asan_report_store2(uptr addr) {
  __asan_abi_report_store_n((void *)addr, 2, true);
}
void __asan_report_store4(uptr addr) {
  __asan_abi_report_store_n((void *)addr, 4, true);
}
void __asan_report_store8(uptr addr) {
  __asan_abi_report_store_n((void *)addr, 8, true);
}
void __asan_report_store16(uptr addr) {
  __asan_abi_report_store_n((void *)addr, 16, true);
}
void __asan_report_store_n(uptr addr, uptr size) {
  __asan_abi_report_store_n((void *)addr, size, true);
}

// Functions concerning memory load and store reporting (experimental variants)
void __asan_report_exp_load1(uptr addr, u32 exp) {
  __asan_abi_report_exp_load_n((void *)addr, exp, 1, true);
}
void __asan_report_exp_load2(uptr addr, u32 exp) {
  __asan_abi_report_exp_load_n((void *)addr, exp, 2, true);
}
void __asan_report_exp_load4(uptr addr, u32 exp) {
  __asan_abi_report_exp_load_n((void *)addr, exp, 4, true);
}
void __asan_report_exp_load8(uptr addr, u32 exp) {
  __asan_abi_report_exp_load_n((void *)addr, exp, 8, true);
}
void __asan_report_exp_load16(uptr addr, u32 exp) {
  __asan_abi_report_exp_load_n((void *)addr, exp, 16, true);
}
void __asan_report_exp_load_n(uptr addr, uptr size, u32 exp) {
  __asan_abi_report_exp_load_n((void *)addr, size, exp, true);
}
void __asan_report_exp_store1(uptr addr, u32 exp) {
  __asan_abi_report_exp_store_n((void *)addr, exp, 1, true);
}
void __asan_report_exp_store2(uptr addr, u32 exp) {
  __asan_abi_report_exp_store_n((void *)addr, exp, 2, true);
}
void __asan_report_exp_store4(uptr addr, u32 exp) {
  __asan_abi_report_exp_store_n((void *)addr, exp, 4, true);
}
void __asan_report_exp_store8(uptr addr, u32 exp) {
  __asan_abi_report_exp_store_n((void *)addr, exp, 8, true);
}
void __asan_report_exp_store16(uptr addr, u32 exp) {
  __asan_abi_report_exp_store_n((void *)addr, exp, 16, true);
}
void __asan_report_exp_store_n(uptr addr, uptr size, u32 exp) {
  __asan_abi_report_exp_store_n((void *)addr, size, exp, true);
}

// Functions concerning memory load and store reporting (noabort variants)
void __asan_report_load1_noabort(uptr addr) {
  __asan_abi_report_load_n((void *)addr, 1, false);
}
void __asan_report_load2_noabort(uptr addr) {
  __asan_abi_report_load_n((void *)addr, 2, false);
}
void __asan_report_load4_noabort(uptr addr) {
  __asan_abi_report_load_n((void *)addr, 4, false);
}
void __asan_report_load8_noabort(uptr addr) {
  __asan_abi_report_load_n((void *)addr, 8, false);
}
void __asan_report_load16_noabort(uptr addr) {
  __asan_abi_report_load_n((void *)addr, 16, false);
}
void __asan_report_load_n_noabort(uptr addr, uptr size) {
  __asan_abi_report_load_n((void *)addr, size, false);
}
void __asan_report_store1_noabort(uptr addr) {
  __asan_abi_report_store_n((void *)addr, 1, false);
}
void __asan_report_store2_noabort(uptr addr) {
  __asan_abi_report_store_n((void *)addr, 2, false);
}
void __asan_report_store4_noabort(uptr addr) {
  __asan_abi_report_store_n((void *)addr, 4, false);
}
void __asan_report_store8_noabort(uptr addr) {
  __asan_abi_report_store_n((void *)addr, 8, false);
}
void __asan_report_store16_noabort(uptr addr) {
  __asan_abi_report_store_n((void *)addr, 16, false);
}
void __asan_report_store_n_noabort(uptr addr, uptr size) {
  __asan_abi_report_store_n((void *)addr, size, false);
}

// Functions concerning memory load and store
void __asan_load1(uptr addr) { __asan_abi_load_n((void *)addr, 1, true); }
void __asan_load2(uptr addr) { __asan_abi_load_n((void *)addr, 2, true); }
void __asan_load4(uptr addr) { __asan_abi_load_n((void *)addr, 4, true); }
void __asan_load8(uptr addr) { __asan_abi_load_n((void *)addr, 8, true); }
void __asan_load16(uptr addr) { __asan_abi_load_n((void *)addr, 16, true); }
void __asan_loadN(uptr addr, uptr size) {
  __asan_abi_load_n((void *)addr, size, true);
}
void __asan_store1(uptr addr) { __asan_abi_store_n((void *)addr, 1, true); }
void __asan_store2(uptr addr) { __asan_abi_store_n((void *)addr, 2, true); }
void __asan_store4(uptr addr) { __asan_abi_store_n((void *)addr, 4, true); }
void __asan_store8(uptr addr) { __asan_abi_store_n((void *)addr, 8, true); }
void __asan_store16(uptr addr) { __asan_abi_store_n((void *)addr, 16, true); }
void __asan_storeN(uptr addr, uptr size) {
  __asan_abi_store_n((void *)addr, size, true);
}

// Functions concerning memory load and store (experimental variants)
void __asan_exp_load1(uptr addr, u32 exp) {
  __asan_abi_exp_load_n((void *)addr, 1, exp, true);
}
void __asan_exp_load2(uptr addr, u32 exp) {
  __asan_abi_exp_load_n((void *)addr, 2, exp, true);
}
void __asan_exp_load4(uptr addr, u32 exp) {
  __asan_abi_exp_load_n((void *)addr, 4, exp, true);
}
void __asan_exp_load8(uptr addr, u32 exp) {
  __asan_abi_exp_load_n((void *)addr, 8, exp, true);
}
void __asan_exp_load16(uptr addr, u32 exp) {
  __asan_abi_exp_load_n((void *)addr, 16, exp, true);
}
void __asan_exp_loadN(uptr addr, uptr size, u32 exp) {
  __asan_abi_exp_load_n((void *)addr, size, exp, true);
}
void __asan_exp_store1(uptr addr, u32 exp) {
  __asan_abi_exp_store_n((void *)addr, 1, exp, true);
}
void __asan_exp_store2(uptr addr, u32 exp) {
  __asan_abi_exp_store_n((void *)addr, 2, exp, true);
}
void __asan_exp_store4(uptr addr, u32 exp) {
  __asan_abi_exp_store_n((void *)addr, 4, exp, true);
}
void __asan_exp_store8(uptr addr, u32 exp) {
  __asan_abi_exp_store_n((void *)addr, 8, exp, true);
}
void __asan_exp_store16(uptr addr, u32 exp) {
  __asan_abi_exp_store_n((void *)addr, 16, exp, true);
}
void __asan_exp_storeN(uptr addr, uptr size, u32 exp) {
  __asan_abi_exp_store_n((void *)addr, size, exp, true);
}

// Functions concerning memory load and store (noabort variants)
void __asan_load1_noabort(uptr addr) {
  __asan_abi_load_n((void *)addr, 1, false);
}
void __asan_load2_noabort(uptr addr) {
  __asan_abi_load_n((void *)addr, 2, false);
}
void __asan_load4_noabort(uptr addr) {
  __asan_abi_load_n((void *)addr, 4, false);
}
void __asan_load8_noabort(uptr addr) {
  __asan_abi_load_n((void *)addr, 8, false);
}
void __asan_load16_noabort(uptr addr) {
  __asan_abi_load_n((void *)addr, 16, false);
}
void __asan_loadN_noabort(uptr addr, uptr size) {
  __asan_abi_load_n((void *)addr, size, false);
}
void __asan_store1_noabort(uptr addr) {
  __asan_abi_store_n((void *)addr, 1, false);
}
void __asan_store2_noabort(uptr addr) {
  __asan_abi_store_n((void *)addr, 2, false);
}
void __asan_store4_noabort(uptr addr) {
  __asan_abi_store_n((void *)addr, 4, false);
}
void __asan_store8_noabort(uptr addr) {
  __asan_abi_store_n((void *)addr, 8, false);
}
void __asan_store16_noabort(uptr addr) {
  __asan_abi_store_n((void *)addr, 16, false);
}
void __asan_storeN_noabort(uptr addr, uptr size) {
  __asan_abi_store_n((void *)addr, size, false);
}

// Functions concerning query about whether memory is poisoned
int __asan_address_is_poisoned(void const volatile *addr) {
  return __asan_abi_address_is_poisoned(addr);
}
uptr __asan_region_is_poisoned(uptr beg, uptr size) {
  return (uptr)__asan_abi_region_is_poisoned((void *)beg, size);
}

// Functions concerning the poisoning of memory
void __asan_poison_memory_region(void const volatile *addr, uptr size) {
  __asan_abi_poison_memory_region(addr, size);
}
void __asan_unpoison_memory_region(void const volatile *addr, uptr size) {
  __asan_abi_unpoison_memory_region(addr, size);
}

// Functions concerning the partial poisoning of memory
void __asan_set_shadow_00(uptr addr, uptr size) {
  __asan_abi_set_shadow_xx_n((void *)addr, 0x00, size);
}
void __asan_set_shadow_01(uptr addr, uptr size) {
  __asan_abi_set_shadow_xx_n((void *)addr, 0x01, size);
}
void __asan_set_shadow_02(uptr addr, uptr size) {
  __asan_abi_set_shadow_xx_n((void *)addr, 0x02, size);
}
void __asan_set_shadow_03(uptr addr, uptr size) {
  __asan_abi_set_shadow_xx_n((void *)addr, 0x03, size);
}
void __asan_set_shadow_04(uptr addr, uptr size) {
  __asan_abi_set_shadow_xx_n((void *)addr, 0x04, size);
}
void __asan_set_shadow_05(uptr addr, uptr size) {
  __asan_abi_set_shadow_xx_n((void *)addr, 0x05, size);
}
void __asan_set_shadow_06(uptr addr, uptr size) {
  __asan_abi_set_shadow_xx_n((void *)addr, 0x06, size);
}
void __asan_set_shadow_07(uptr addr, uptr size) {
  __asan_abi_set_shadow_xx_n((void *)addr, 0x07, size);
}
void __asan_set_shadow_f1(uptr addr, uptr size) {
  __asan_abi_set_shadow_xx_n((void *)addr, 0xf1, size);
}
void __asan_set_shadow_f2(uptr addr, uptr size) {
  __asan_abi_set_shadow_xx_n((void *)addr, 0xf2, size);
}
void __asan_set_shadow_f3(uptr addr, uptr size) {
  __asan_abi_set_shadow_xx_n((void *)addr, 0xf3, size);
}
void __asan_set_shadow_f5(uptr addr, uptr size) {
  __asan_abi_set_shadow_xx_n((void *)addr, 0xf5, size);
}
void __asan_set_shadow_f8(uptr addr, uptr size) {
  __asan_abi_set_shadow_xx_n((void *)addr, 0xf8, size);
}

// Functions concerning stack poisoning
void __asan_poison_stack_memory(uptr addr, uptr size) {
  __asan_abi_poison_stack_memory((void *)addr, size);
}
void __asan_unpoison_stack_memory(uptr addr, uptr size) {
  __asan_abi_unpoison_stack_memory((void *)addr, size);
}

// Functions concerning redzone poisoning
void __asan_poison_intra_object_redzone(uptr p, uptr size) {
  __asan_abi_poison_intra_object_redzone((void *)p, size);
}
void __asan_unpoison_intra_object_redzone(uptr p, uptr size) {
  __asan_abi_unpoison_intra_object_redzone((void *)p, size);
}

// Functions concerning array cookie poisoning
void __asan_poison_cxx_array_cookie(uptr p) {
  __asan_abi_poison_cxx_array_cookie((void *)p);
}
uptr __asan_load_cxx_array_cookie(uptr *p) {
  return (uptr)__asan_abi_load_cxx_array_cookie((void **)p);
}

// Functions concerning fake stacks
void *__asan_get_current_fake_stack(void) {
  return __asan_abi_get_current_fake_stack();
}
void *__asan_addr_is_in_fake_stack(void *fake_stack, void *addr, void **beg,
                                   void **end) {
  return __asan_abi_addr_is_in_fake_stack(fake_stack, addr, beg, end);
}

// Functions concerning poisoning and unpoisoning fake stack alloca
void __asan_alloca_poison(uptr addr, uptr size) {
  __asan_abi_alloca_poison((void *)addr, size);
}
void __asan_allocas_unpoison(uptr top, uptr bottom) {
  __asan_abi_allocas_unpoison((void *)top, (void *)bottom);
}

// Functions concerning fake stack malloc
uptr __asan_stack_malloc_0(uptr size) {
  return (uptr)__asan_abi_stack_malloc_n(0, size);
}
uptr __asan_stack_malloc_1(uptr size) {
  return (uptr)__asan_abi_stack_malloc_n(1, size);
}
uptr __asan_stack_malloc_2(uptr size) {
  return (uptr)__asan_abi_stack_malloc_n(2, size);
}
uptr __asan_stack_malloc_3(uptr size) {
  return (uptr)__asan_abi_stack_malloc_n(3, size);
}
uptr __asan_stack_malloc_4(uptr size) {
  return (uptr)__asan_abi_stack_malloc_n(4, size);
}
uptr __asan_stack_malloc_5(uptr size) {
  return (uptr)__asan_abi_stack_malloc_n(5, size);
}
uptr __asan_stack_malloc_6(uptr size) {
  return (uptr)__asan_abi_stack_malloc_n(6, size);
}
uptr __asan_stack_malloc_7(uptr size) {
  return (uptr)__asan_abi_stack_malloc_n(7, size);
}
uptr __asan_stack_malloc_8(uptr size) {
  return (uptr)__asan_abi_stack_malloc_n(8, size);
}
uptr __asan_stack_malloc_9(uptr size) {
  return (uptr)__asan_abi_stack_malloc_n(9, size);
}
uptr __asan_stack_malloc_10(uptr size) {
  return (uptr)__asan_abi_stack_malloc_n(10, size);
}

// Functions concerning fake stack malloc (always variants)
uptr __asan_stack_malloc_always_0(uptr size) {
  return (uptr)__asan_abi_stack_malloc_always_n(0, size);
}
uptr __asan_stack_malloc_always_1(uptr size) {
  return (uptr)__asan_abi_stack_malloc_always_n(1, size);
}
uptr __asan_stack_malloc_always_2(uptr size) {
  return (uptr)__asan_abi_stack_malloc_always_n(2, size);
}
uptr __asan_stack_malloc_always_3(uptr size) {
  return (uptr)__asan_abi_stack_malloc_always_n(3, size);
}
uptr __asan_stack_malloc_always_4(uptr size) {
  return (uptr)__asan_abi_stack_malloc_always_n(4, size);
}
uptr __asan_stack_malloc_always_5(uptr size) {
  return (uptr)__asan_abi_stack_malloc_always_n(5, size);
}
uptr __asan_stack_malloc_always_6(uptr size) {
  return (uptr)__asan_abi_stack_malloc_always_n(6, size);
}
uptr __asan_stack_malloc_always_7(uptr size) {
  return (uptr)__asan_abi_stack_malloc_always_n(7, size);
}
uptr __asan_stack_malloc_always_8(uptr size) {
  return (uptr)__asan_abi_stack_malloc_always_n(8, size);
}
uptr __asan_stack_malloc_always_9(uptr size) {
  return (uptr)__asan_abi_stack_malloc_always_n(9, size);
}
uptr __asan_stack_malloc_always_10(uptr size) {
  return (uptr)__asan_abi_stack_malloc_always_n(10, size);
}

// Functions concerning fake stack free
void __asan_stack_free_0(uptr ptr, uptr size) {
  __asan_abi_stack_free_n(0, (void *)ptr, size);
}
void __asan_stack_free_1(uptr ptr, uptr size) {
  __asan_abi_stack_free_n(1, (void *)ptr, size);
}
void __asan_stack_free_2(uptr ptr, uptr size) {
  __asan_abi_stack_free_n(2, (void *)ptr, size);
}
void __asan_stack_free_3(uptr ptr, uptr size) {
  __asan_abi_stack_free_n(3, (void *)ptr, size);
}
void __asan_stack_free_4(uptr ptr, uptr size) {
  __asan_abi_stack_free_n(4, (void *)ptr, size);
}
void __asan_stack_free_5(uptr ptr, uptr size) {
  __asan_abi_stack_free_n(5, (void *)ptr, size);
}
void __asan_stack_free_6(uptr ptr, uptr size) {
  __asan_abi_stack_free_n(6, (void *)ptr, size);
}
void __asan_stack_free_7(uptr ptr, uptr size) {
  __asan_abi_stack_free_n(7, (void *)ptr, size);
}
void __asan_stack_free_8(uptr ptr, uptr size) {
  __asan_abi_stack_free_n(8, (void *)ptr, size);
}
void __asan_stack_free_9(uptr ptr, uptr size) {
  __asan_abi_stack_free_n(9, (void *)ptr, size);
}
void __asan_stack_free_10(uptr ptr, uptr size) {
  __asan_abi_stack_free_n(10, (void *)ptr, size);
}
}
