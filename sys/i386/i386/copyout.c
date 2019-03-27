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

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_extern.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_page.h>

int copyin_fast(const void *udaddr, void *kaddr, size_t len, u_int);
static int (*copyin_fast_tramp)(const void *, void *, size_t, u_int);
int copyout_fast(const void *kaddr, void *udaddr, size_t len, u_int);
static int (*copyout_fast_tramp)(const void *, void *, size_t, u_int);
int fubyte_fast(volatile const void *base, u_int kcr3);
static int (*fubyte_fast_tramp)(volatile const void *, u_int);
int fuword16_fast(volatile const void *base, u_int kcr3);
static int (*fuword16_fast_tramp)(volatile const void *, u_int);
int fueword_fast(volatile const void *base, long *val, u_int kcr3);
static int (*fueword_fast_tramp)(volatile const void *, long *, u_int);
int subyte_fast(volatile void *base, int val, u_int kcr3);
static int (*subyte_fast_tramp)(volatile void *, int, u_int);
int suword16_fast(volatile void *base, int val, u_int kcr3);
static int (*suword16_fast_tramp)(volatile void *, int, u_int);
int suword_fast(volatile void *base, long val, u_int kcr3);
static int (*suword_fast_tramp)(volatile void *, long, u_int);

static int fast_copyout = 1;
SYSCTL_INT(_machdep, OID_AUTO, fast_copyout, CTLFLAG_RWTUN,
    &fast_copyout, 0,
    "");

void
copyout_init_tramp(void)
{

	copyin_fast_tramp = (int (*)(const void *, void *, size_t, u_int))(
	    (uintptr_t)copyin_fast + setidt_disp);
	copyout_fast_tramp = (int (*)(const void *, void *, size_t, u_int))(
	    (uintptr_t)copyout_fast + setidt_disp);
	fubyte_fast_tramp = (int (*)(volatile const void *, u_int))(
	    (uintptr_t)fubyte_fast + setidt_disp);
	fuword16_fast_tramp = (int (*)(volatile const void *, u_int))(
	    (uintptr_t)fuword16_fast + setidt_disp);
	fueword_fast_tramp = (int (*)(volatile const void *, long *, u_int))(
	    (uintptr_t)fueword_fast + setidt_disp);
	subyte_fast_tramp = (int (*)(volatile void *, int, u_int))(
	    (uintptr_t)subyte_fast + setidt_disp);
	suword16_fast_tramp = (int (*)(volatile void *, int, u_int))(
	    (uintptr_t)suword16_fast + setidt_disp);
	suword_fast_tramp = (int (*)(volatile void *, long, u_int))(
	    (uintptr_t)suword_fast + setidt_disp);
}

int
cp_slow0(vm_offset_t uva, size_t len, bool write,
    void (*f)(vm_offset_t, void *), void *arg)
{
	struct pcpu *pc;
	vm_page_t m[2];
	vm_offset_t kaddr;
	int error, i, plen;
	bool sleepable;

	plen = howmany(uva - trunc_page(uva) + len, PAGE_SIZE);
	MPASS(plen <= nitems(m));
	error = 0;
	i = vm_fault_quick_hold_pages(&curproc->p_vmspace->vm_map, uva, len,
	    (write ? VM_PROT_WRITE : VM_PROT_READ) | VM_PROT_QUICK_NOFAULT,
	    m, nitems(m));
	if (i != plen)
		return (EFAULT);
	sched_pin();
	pc = get_pcpu();
	if (!THREAD_CAN_SLEEP() || curthread->td_vslock_sz > 0 ||
	    (curthread->td_pflags & TDP_NOFAULTING) != 0) {
		sleepable = false;
		mtx_lock(&pc->pc_copyout_mlock);
		kaddr = pc->pc_copyout_maddr;
	} else {
		sleepable = true;
		sx_xlock(&pc->pc_copyout_slock);
		kaddr = pc->pc_copyout_saddr;
	}
	pmap_cp_slow0_map(kaddr, plen, m);
	kaddr += uva - trunc_page(uva);
	f(kaddr, arg);
	sched_unpin();
	if (sleepable)
		sx_xunlock(&pc->pc_copyout_slock);
	else
		mtx_unlock(&pc->pc_copyout_mlock);
	vm_page_unhold_pages(m, plen);
	return (error);
}

struct copyinstr_arg0 {
	vm_offset_t kc;
	size_t len;
	size_t alen;
	bool end;
};

static void
copyinstr_slow0(vm_offset_t kva, void *arg)
{
	struct copyinstr_arg0 *ca;
	char c;

	ca = arg;
	MPASS(ca->alen == 0 && ca->len > 0 && !ca->end);
	while (ca->alen < ca->len && !ca->end) {
		c = *(char *)(kva + ca->alen);
		*(char *)ca->kc = c;
		ca->alen++;
		ca->kc++;
		if (c == '\0')
			ca->end = true;
	}
}

int
copyinstr(const void *udaddr, void *kaddr, size_t maxlen, size_t *lencopied)
{
	struct copyinstr_arg0 ca;
	vm_offset_t uc;
	size_t plen;
	int error;

	error = 0;
	ca.end = false;
	for (plen = 0, uc = (vm_offset_t)udaddr, ca.kc = (vm_offset_t)kaddr;
	    plen < maxlen && !ca.end; uc += ca.alen, plen += ca.alen) {
		ca.len = round_page(uc) - uc;
		if (ca.len == 0)
			ca.len = PAGE_SIZE;
		if (plen + ca.len > maxlen)
			ca.len = maxlen - plen;
		ca.alen = 0;
		if (cp_slow0(uc, ca.len, false, copyinstr_slow0, &ca) != 0) {
			error = EFAULT;
			break;
		}
	}
	if (!ca.end && plen == maxlen && error == 0)
		error = ENAMETOOLONG;
	if (lencopied != NULL)
		*lencopied = plen;
	return (error);
}

struct copyin_arg0 {
	vm_offset_t kc;
	size_t len;
};

static void
copyin_slow0(vm_offset_t kva, void *arg)
{
	struct copyin_arg0 *ca;

	ca = arg;
	bcopy((void *)kva, (void *)ca->kc, ca->len);
}

int
copyin(const void *udaddr, void *kaddr, size_t len)
{
	struct copyin_arg0 ca;
	vm_offset_t uc;
	size_t plen;

	if ((uintptr_t)udaddr + len < (uintptr_t)udaddr ||
	    (uintptr_t)udaddr + len > VM_MAXUSER_ADDRESS)
		return (EFAULT);
	if (len == 0 || (fast_copyout && len <= TRAMP_COPYOUT_SZ &&
	    copyin_fast_tramp(udaddr, kaddr, len, pmap_get_kcr3()) == 0))
		return (0);
	for (plen = 0, uc = (vm_offset_t)udaddr, ca.kc = (vm_offset_t)kaddr;
	    plen < len; uc += ca.len, ca.kc += ca.len, plen += ca.len) {
		ca.len = round_page(uc) - uc;
		if (ca.len == 0)
			ca.len = PAGE_SIZE;
		if (plen + ca.len > len)
			ca.len = len - plen;
		if (cp_slow0(uc, ca.len, false, copyin_slow0, &ca) != 0)
			return (EFAULT);
	}
	return (0);
}

static void
copyout_slow0(vm_offset_t kva, void *arg)
{
	struct copyin_arg0 *ca;

	ca = arg;
	bcopy((void *)ca->kc, (void *)kva, ca->len);
}

int
copyout(const void *kaddr, void *udaddr, size_t len)
{
	struct copyin_arg0 ca;
	vm_offset_t uc;
	size_t plen;

	if ((uintptr_t)udaddr + len < (uintptr_t)udaddr ||
	    (uintptr_t)udaddr + len > VM_MAXUSER_ADDRESS)
		return (EFAULT);
	if (len == 0 || (fast_copyout && len <= TRAMP_COPYOUT_SZ &&
	    copyout_fast_tramp(kaddr, udaddr, len, pmap_get_kcr3()) == 0))
		return (0);
	for (plen = 0, uc = (vm_offset_t)udaddr, ca.kc = (vm_offset_t)kaddr;
	    plen < len; uc += ca.len, ca.kc += ca.len, plen += ca.len) {
		ca.len = round_page(uc) - uc;
		if (ca.len == 0)
			ca.len = PAGE_SIZE;
		if (plen + ca.len > len)
			ca.len = len - plen;
		if (cp_slow0(uc, ca.len, true, copyout_slow0, &ca) != 0)
			return (EFAULT);
	}
	return (0);
}

/*
 * Fetch (load) a 32-bit word, a 16-bit word, or an 8-bit byte from user
 * memory.
 */

static void
fubyte_slow0(vm_offset_t kva, void *arg)
{

	*(int *)arg = *(u_char *)kva;
}

int
fubyte(volatile const void *base)
{
	int res;

	if ((uintptr_t)base + sizeof(uint8_t) < (uintptr_t)base ||
	    (uintptr_t)base + sizeof(uint8_t) > VM_MAXUSER_ADDRESS)
		return (-1);
	if (fast_copyout) {
		res = fubyte_fast_tramp(base, pmap_get_kcr3());
		if (res != -1)
			return (res);
	}
	if (cp_slow0((vm_offset_t)base, sizeof(char), false, fubyte_slow0,
	    &res) != 0)
		return (-1);
	return (res);
}

static void
fuword16_slow0(vm_offset_t kva, void *arg)
{

	*(int *)arg = *(uint16_t *)kva;
}

int
fuword16(volatile const void *base)
{
	int res;

	if ((uintptr_t)base + sizeof(uint16_t) < (uintptr_t)base ||
	    (uintptr_t)base + sizeof(uint16_t) > VM_MAXUSER_ADDRESS)
		return (-1);
	if (fast_copyout) {
		res = fuword16_fast_tramp(base, pmap_get_kcr3());
		if (res != -1)
			return (res);
	}
	if (cp_slow0((vm_offset_t)base, sizeof(uint16_t), false,
	    fuword16_slow0, &res) != 0)
		return (-1);
	return (res);
}

static void
fueword_slow0(vm_offset_t kva, void *arg)
{

	*(uint32_t *)arg = *(uint32_t *)kva;
}

int
fueword(volatile const void *base, long *val)
{
	uint32_t res;

	if ((uintptr_t)base + sizeof(*val) < (uintptr_t)base ||
	    (uintptr_t)base + sizeof(*val) > VM_MAXUSER_ADDRESS)
		return (-1);
	if (fast_copyout) {
		if (fueword_fast_tramp(base, val, pmap_get_kcr3()) == 0)
			return (0);
	}
	if (cp_slow0((vm_offset_t)base, sizeof(long), false, fueword_slow0,
	    &res) != 0)
		return (-1);
	*val = res;
	return (0);
}

int
fueword32(volatile const void *base, int32_t *val)
{

	return (fueword(base, (long *)val));
}

/*
 * Store a 32-bit word, a 16-bit word, or an 8-bit byte to user memory.
 */

static void
subyte_slow0(vm_offset_t kva, void *arg)
{

	*(u_char *)kva = *(int *)arg;
}

int
subyte(volatile void *base, int byte)
{

	if ((uintptr_t)base + sizeof(uint8_t) < (uintptr_t)base ||
	    (uintptr_t)base + sizeof(uint8_t) > VM_MAXUSER_ADDRESS)
		return (-1);
	if (fast_copyout && subyte_fast_tramp(base, byte, pmap_get_kcr3()) == 0)
		return (0);
	return (cp_slow0((vm_offset_t)base, sizeof(u_char), true, subyte_slow0,
	    &byte) != 0 ? -1 : 0);
}

static void
suword16_slow0(vm_offset_t kva, void *arg)
{

	*(int *)kva = *(uint16_t *)arg;
}

int
suword16(volatile void *base, int word)
{

	if ((uintptr_t)base + sizeof(uint16_t) < (uintptr_t)base ||
	    (uintptr_t)base + sizeof(uint16_t) > VM_MAXUSER_ADDRESS)
		return (-1);
	if (fast_copyout && suword16_fast_tramp(base, word, pmap_get_kcr3())
	    == 0)
		return (0);
	return (cp_slow0((vm_offset_t)base, sizeof(int16_t), true,
	    suword16_slow0, &word) != 0 ? -1 : 0);
}

static void
suword_slow0(vm_offset_t kva, void *arg)
{

	*(int *)kva = *(uint32_t *)arg;
}

int
suword(volatile void *base, long word)
{

	if ((uintptr_t)base + sizeof(word) < (uintptr_t)base ||
	    (uintptr_t)base + sizeof(word) > VM_MAXUSER_ADDRESS)
		return (-1);
	if (fast_copyout && suword_fast_tramp(base, word, pmap_get_kcr3()) == 0)
		return (0);
	return (cp_slow0((vm_offset_t)base, sizeof(long), true,
	    suword_slow0, &word) != 0 ? -1 : 0);
}

int
suword32(volatile void *base, int32_t word)
{

	return (suword(base, word));
}

struct casueword_arg0 {
	uint32_t oldval;
	uint32_t newval;
};

static void
casueword_slow0(vm_offset_t kva, void *arg)
{
	struct casueword_arg0 *ca;

	ca = arg;
	atomic_fcmpset_int((u_int *)kva, &ca->oldval, ca->newval);
}

int
casueword32(volatile uint32_t *base, uint32_t oldval, uint32_t *oldvalp,
    uint32_t newval)
{
	struct casueword_arg0 ca;
	int res;

	ca.oldval = oldval;
	ca.newval = newval;
	res = cp_slow0((vm_offset_t)base, sizeof(int32_t), true,
	    casueword_slow0, &ca);
	if (res == 0) {
		*oldvalp = ca.oldval;
		return (0);
	}
	return (-1);
}

int
casueword(volatile u_long *base, u_long oldval, u_long *oldvalp, u_long newval)
{
	struct casueword_arg0 ca;
	int res;

	ca.oldval = oldval;
	ca.newval = newval;
	res = cp_slow0((vm_offset_t)base, sizeof(int32_t), true,
	    casueword_slow0, &ca);
	if (res == 0) {
		*oldvalp = ca.oldval;
		return (0);
	}
	return (-1);
}
