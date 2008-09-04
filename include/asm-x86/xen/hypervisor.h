/******************************************************************************
 * hypervisor.h
 *
 * Linux-specific hypervisor handling.
 *
 * Copyright (c) 2002-2004, K A Fraser
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef ASM_X86__XEN__HYPERVISOR_H
#define ASM_X86__XEN__HYPERVISOR_H

#include <linux/types.h>
#include <linux/kernel.h>

#include <xen/interface/xen.h>
#include <xen/interface/version.h>

#include <asm/ptrace.h>
#include <asm/page.h>
#include <asm/desc.h>
#if defined(__i386__)
#  ifdef CONFIG_X86_PAE
#   include <asm-generic/pgtable-nopud.h>
#  else
#   include <asm-generic/pgtable-nopmd.h>
#  endif
#endif
#include <asm/xen/hypercall.h>

/* arch/i386/kernel/setup.c */
extern struct shared_info *HYPERVISOR_shared_info;
extern struct start_info *xen_start_info;
#define is_initial_xendomain() (xen_start_info->flags & SIF_INITDOMAIN)

/* arch/i386/mach-xen/evtchn.c */
/* Force a proper event-channel callback from Xen. */
extern void force_evtchn_callback(void);

/* Turn jiffies into Xen system time. */
u64 jiffies_to_st(unsigned long jiffies);


#define MULTI_UVMFLAGS_INDEX 3
#define MULTI_UVMDOMID_INDEX 4

#define is_running_on_xen()	(xen_start_info ? 1 : 0)

#endif /* ASM_X86__XEN__HYPERVISOR_H */
