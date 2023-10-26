/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LINUX_KALLSYMS_INTERNAL_H_
#define LINUX_KALLSYMS_INTERNAL_H_

#include <linux/types.h>

/*
 * These will be re-linked against their real values
 * during the second link stage.
 */
extern const unsigned long kallsyms_addresses[] __weak;
extern const int kallsyms_offsets[] __weak;
extern const u8 kallsyms_names[] __weak;

/*
 * Tell the compiler that the count isn't in the small data section if the arch
 * has one (eg: FRV).
 */
extern const unsigned int kallsyms_num_syms
__section(".rodata") __attribute__((weak));

extern const unsigned long kallsyms_relative_base
__section(".rodata") __attribute__((weak));

extern const char kallsyms_token_table[] __weak;
extern const u16 kallsyms_token_index[] __weak;

extern const unsigned int kallsyms_markers[] __weak;
extern const unsigned int kallsyms_seqs_of_names[] __weak;

#endif // LINUX_KALLSYMS_INTERNAL_H_
