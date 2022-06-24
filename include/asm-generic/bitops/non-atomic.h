/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_GENERIC_BITOPS_NON_ATOMIC_H_
#define _ASM_GENERIC_BITOPS_NON_ATOMIC_H_

#include <asm-generic/bitops/generic-non-atomic.h>

#define arch___set_bit generic___set_bit
#define __set_bit arch___set_bit

#define arch___clear_bit generic___clear_bit
#define __clear_bit arch___clear_bit

#define arch___change_bit generic___change_bit
#define __change_bit arch___change_bit

#define arch___test_and_set_bit generic___test_and_set_bit
#define __test_and_set_bit arch___test_and_set_bit

#define arch___test_and_clear_bit generic___test_and_clear_bit
#define __test_and_clear_bit arch___test_and_clear_bit

#define arch___test_and_change_bit generic___test_and_change_bit
#define __test_and_change_bit arch___test_and_change_bit

#define arch_test_bit generic_test_bit
#define test_bit arch_test_bit

#endif /* _ASM_GENERIC_BITOPS_NON_ATOMIC_H_ */
