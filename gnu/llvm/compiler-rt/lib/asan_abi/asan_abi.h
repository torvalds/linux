//===-asan_abi.h - ASan Stable ABI Interface-------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef ASAN_ABI_H
#define ASAN_ABI_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/cdefs.h>
#include <sys/types.h>

__BEGIN_DECLS

// Functions concerning instrumented global variables:
void __asan_abi_register_image_globals();
void __asan_abi_unregister_image_globals();
void __asan_abi_register_elf_globals(bool *flag, void *start, void *stop);
void __asan_abi_unregister_elf_globals(bool *flag, void *start, void *stop);
void __asan_abi_register_globals(void *globals, size_t n);
void __asan_abi_unregister_globals(void *globals, size_t n);

// Functions concerning dynamic library initialization
void __asan_abi_before_dynamic_init(const char *module_name);
void __asan_abi_after_dynamic_init();

// Functions concerning block memory destinations
void *__asan_abi_memcpy(void *d, const void *s, size_t n);
void *__asan_abi_memmove(void *d, const void *s, size_t n);
void *__asan_abi_memset(void *p, int c, size_t n);

// Functions concerning RTL startup and initialization
void __asan_abi_init();
void __asan_abi_handle_no_return();

// Functions concerning memory load and store reporting
void __asan_abi_report_load_n(void *p, size_t n, bool abort);
void __asan_abi_report_exp_load_n(void *p, size_t n, int exp, bool abort);
void __asan_abi_report_store_n(void *p, size_t n, bool abort);
void __asan_abi_report_exp_store_n(void *p, size_t n, int exp, bool abort);

// Functions concerning memory load and store
void __asan_abi_load_n(void *p, size_t n, bool abort);
void __asan_abi_exp_load_n(void *p, size_t n, int exp, bool abort);
void __asan_abi_store_n(void *p, size_t n, bool abort);
void __asan_abi_exp_store_n(void *p, size_t n, int exp, bool abort);

// Functions concerning query about whether memory is poisoned
int __asan_abi_address_is_poisoned(void const volatile *p);
void *__asan_abi_region_is_poisoned(void const volatile *p, size_t size);

// Functions concerning the poisoning of memory
void __asan_abi_unpoison_memory_region(void const volatile *p, size_t n);
void __asan_abi_poison_memory_region(void const volatile *p, size_t n);

// Functions concerning the partial poisoning of memory
void __asan_abi_set_shadow_xx_n(void *p, unsigned char xx, size_t n);

// Functions concerning stack poisoning
void __asan_abi_poison_stack_memory(void *p, size_t n);
void __asan_abi_unpoison_stack_memory(void *p, size_t n);

// Functions concerning redzone poisoning
void __asan_abi_poison_intra_object_redzone(void *p, size_t size);
void __asan_abi_unpoison_intra_object_redzone(void *p, size_t size);

// Functions concerning array cookie poisoning
void __asan_abi_poison_cxx_array_cookie(void *p);
void *__asan_abi_load_cxx_array_cookie(void **p);

// Functions concerning fake stacks
void *__asan_abi_get_current_fake_stack();
void *__asan_abi_addr_is_in_fake_stack(void *fake_stack, void *addr, void **beg,
                                       void **end);
// Functions concerning poisoning and unpoisoning fake stack alloca
void __asan_abi_alloca_poison(void *addr, size_t size);
void __asan_abi_allocas_unpoison(void *top, void *bottom);

// Functions concerning fake stack malloc
void *__asan_abi_stack_malloc_n(size_t scale, size_t size);
void *__asan_abi_stack_malloc_always_n(size_t scale, size_t size);

// Functions concerning fake stack free
void __asan_abi_stack_free_n(int scale, void *p, size_t n);

__END_DECLS
#endif // ASAN_ABI_H
