/*-
 * Copyright (c) 2018 VMware, Inc.
 *
 * SPDX-License-Identifier: (BSD-2-Clause OR GPL-2.0)
 *
 * $FreeBSD$
 */

/* Some common utilities used by the VMCI kernel module. */

#ifndef _VMCI_KERNEL_DEFS_H_
#define _VMCI_KERNEL_DEFS_H_

#include <sys/param.h>
#include <sys/systm.h>

typedef uint32_t PPN;

#define ASSERT(cond)		KASSERT(cond, ("%s", #cond))
#define ASSERT_ON_COMPILE(e)	_Static_assert(e, #e);

#define LIKELY(_exp)		__predict_true(_exp)
#define UNLIKELY(_exp)		__predict_false(_exp)

#define CONST64U(c)		UINT64_C(c)

#define ARRAYSIZE(a)		nitems(a)

#define ROUNDUP(x, y)		roundup(x, y)
#define CEILING(x, y)		howmany(x, y)

#endif /* !_VMCI_KERNEL_DEFS_H_ */
