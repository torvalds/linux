/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SELFTEST_KVM_UTIL_TYPES_H
#define SELFTEST_KVM_UTIL_TYPES_H

#include <linux/types.h>

/*
 * Provide a version of static_assert() that is guaranteed to have an optional
 * message param.  _GNU_SOURCE is defined for all KVM selftests, _GNU_SOURCE
 * implies _ISOC11_SOURCE, and if _ISOC11_SOURCE is defined, glibc #undefs and
 * #defines static_assert() as a direct alias to _Static_assert() (see
 * usr/include/assert.h).  Define a custom macro instead of redefining
 * static_assert() to avoid creating non-deterministic behavior that is
 * dependent on include order.
 */
#define __kvm_static_assert(expr, msg, ...) _Static_assert(expr, msg)
#define kvm_static_assert(expr, ...) __kvm_static_assert(expr, ##__VA_ARGS__, #expr)

typedef u64 gpa_t; /* Virtual Machine (Guest) physical address */
typedef u64 gva_t; /* Virtual Machine (Guest) virtual address */

#define INVALID_GPA (~(u64)0)

#endif /* SELFTEST_KVM_UTIL_TYPES_H */
