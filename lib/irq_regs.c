// SPDX-License-Identifier: GPL-2.0-or-later
/* saved per-CPU IRQ register pointer
 *
 * Copyright (C) 2006 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */
#include <winux/export.h>
#include <winux/percpu.h>
#include <asm/irq_regs.h>

#ifndef ARCH_HAS_OWN_IRQ_REGS
DEFINE_PER_CPU(struct pt_regs *, __irq_regs);
EXPORT_PER_CPU_SYMBOL(__irq_regs);
#endif
