/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Konstantin Belousov <kib@FreeBSD.org>
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_compat.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/imgact.h>
#include <sys/lock.h>
#include <sys/sx.h>
#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_extern.h>
#include <vm/pmap.h>

#include <machine/atomic.h>
#include <machine/md_var.h>

#include <compat/linux/linux_emul.h>
#include <compat/linux/linux_futex.h>

struct futex_st0 {
	int oparg;
	int *oldval;
};

static void
futex_xchgl_slow0(vm_offset_t kva, void *arg)
{
	struct futex_st0 *st;

	st = arg;
	*st->oldval = atomic_swap_int((int *)kva, st->oparg);
}

int
futex_xchgl(int oparg, uint32_t *uaddr, int *oldval)
{
	struct futex_st0 st;

	st.oparg = oparg;
	st.oldval = oldval;
	if (cp_slow0((vm_offset_t)uaddr, sizeof(uint32_t), true,
	    futex_xchgl_slow0, &st) != 0)
		return (-EFAULT);
	return (0);
}

static void
futex_addl_slow0(vm_offset_t kva, void *arg)
{
	struct futex_st0 *st;

	st = arg;
	*st->oldval = atomic_fetchadd_int((int *)kva, st->oparg);
}

int
futex_addl(int oparg, uint32_t *uaddr, int *oldval)
{
	struct futex_st0 st;

	st.oparg = oparg;
	st.oldval = oldval;
	if (cp_slow0((vm_offset_t)uaddr, sizeof(uint32_t), true,
	    futex_addl_slow0, &st) != 0)
		return (-EFAULT);
	return (0);
}

static void
futex_orl_slow0(vm_offset_t kva, void *arg)
{
	struct futex_st0 *st;
	int old;

	st = arg;
	old = *(int *)kva;
	while (!atomic_fcmpset_int((int *)kva, &old, old | st->oparg))
		;
	*st->oldval = old;
}

int
futex_orl(int oparg, uint32_t *uaddr, int *oldval)
{
	struct futex_st0 st;

	st.oparg = oparg;
	st.oldval = oldval;
	if (cp_slow0((vm_offset_t)uaddr, sizeof(uint32_t), true,
	    futex_orl_slow0, &st) != 0)
		return (-EFAULT);
	return (0);
}

static void
futex_andl_slow0(vm_offset_t kva, void *arg)
{
	struct futex_st0 *st;
	int old;

	st = arg;
	old = *(int *)kva;
	while (!atomic_fcmpset_int((int *)kva, &old, old & st->oparg))
		;
	*st->oldval = old;
}

int
futex_andl(int oparg, uint32_t *uaddr, int *oldval)
{
	struct futex_st0 st;

	st.oparg = oparg;
	st.oldval = oldval;
	if (cp_slow0((vm_offset_t)uaddr, sizeof(uint32_t), true,
	    futex_andl_slow0, &st) != 0)
		return (-EFAULT);
	return (0);
}

static void
futex_xorl_slow0(vm_offset_t kva, void *arg)
{
	struct futex_st0 *st;
	int old;

	st = arg;
	old = *(int *)kva;
	while (!atomic_fcmpset_int((int *)kva, &old, old ^ st->oparg))
		;
	*st->oldval = old;
}

int
futex_xorl(int oparg, uint32_t *uaddr, int *oldval)
{
	struct futex_st0 st;

	st.oparg = oparg;
	st.oldval = oldval;
	if (cp_slow0((vm_offset_t)uaddr, sizeof(uint32_t), true,
	    futex_xorl_slow0, &st) != 0)
		return (-EFAULT);
	return (0);
}
