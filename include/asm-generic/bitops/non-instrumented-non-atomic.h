/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_GENERIC_BITOPS_NON_INSTRUMENTED_NON_ATOMIC_H
#define __ASM_GENERIC_BITOPS_NON_INSTRUMENTED_NON_ATOMIC_H

#define ___set_bit		arch___set_bit
#define ___clear_bit		arch___clear_bit
#define ___change_bit		arch___change_bit

#define ___test_and_set_bit	arch___test_and_set_bit
#define ___test_and_clear_bit	arch___test_and_clear_bit
#define ___test_and_change_bit	arch___test_and_change_bit

#define _test_bit		arch_test_bit
#define _test_bit_acquire	arch_test_bit_acquire

#endif /* __ASM_GENERIC_BITOPS_NON_INSTRUMENTED_NON_ATOMIC_H */
