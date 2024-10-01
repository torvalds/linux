/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_GENERIC_BITOPS_NON_ATOMIC_H_
#define _ASM_GENERIC_BITOPS_NON_ATOMIC_H_

#include <asm-generic/bitops/generic-non-atomic.h>

#define arch___set_bit generic___set_bit
#define arch___clear_bit generic___clear_bit
#define arch___change_bit generic___change_bit

#define arch___test_and_set_bit generic___test_and_set_bit
#define arch___test_and_clear_bit generic___test_and_clear_bit
#define arch___test_and_change_bit generic___test_and_change_bit

#define arch_test_bit generic_test_bit
#define arch_test_bit_acquire generic_test_bit_acquire

#include <asm-generic/bitops/non-instrumented-non-atomic.h>

#endif /* _ASM_GENERIC_BITOPS_NON_ATOMIC_H_ */
