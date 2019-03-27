/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Jake Burkholder.
 * All rights reserved.
 * Copyright 2001 by Thomas Moestl <tmm@FreeBSD.org>.  All rights reserved.
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/param.h>

#include <vm/vm.h>

#include <machine/asi.h>
#include <machine/cpufunc.h>
#include <machine/lsu.h>
#include <machine/watch.h>

#include <ddb/ddb.h>
#include <ddb/db_access.h>
#include <ddb/db_sym.h>
#include <ddb/db_variables.h>
#include <ddb/db_watch.h>

static void db_watch_print(vm_offset_t wp, int bm);

int
watch_phys_set_mask(vm_paddr_t pa, u_long mask)
{
	u_long lsucr;

	stxa_sync(AA_DMMU_PWPR, ASI_DMMU, pa & (((2UL << 38) - 1) << 3));
	lsucr = ldxa(0, ASI_LSU_CTL_REG);
	lsucr = ((lsucr | LSU_PW) & ~LSU_PM_MASK) |
	    (mask << LSU_PM_SHIFT);
	stxa_sync(0, ASI_LSU_CTL_REG, lsucr);
	return (0);
}

int
watch_phys_set(vm_paddr_t pa, int sz)
{
	u_long off;

	off = (u_long)pa & 7;
	/* Test for misaligned watch points. */
	if (off + sz > 8)
		return (-1);
	return (watch_phys_set_mask(pa, ((1 << sz) - 1) << off));
}

vm_paddr_t
watch_phys_get(int *bm)
{
	vm_paddr_t pa;
	u_long lsucr;
	
	if (!watch_phys_active())
		return (0);

	pa = ldxa(AA_DMMU_PWPR, ASI_DMMU);
	lsucr = ldxa(0, ASI_LSU_CTL_REG);
	*bm = (lsucr & LSU_PM_MASK) >> LSU_PM_SHIFT;
	
	return (pa);
}

void
watch_phys_clear()
{
	stxa_sync(0, ASI_LSU_CTL_REG,
	    ldxa(0, ASI_LSU_CTL_REG) & ~LSU_PW);
}

int
watch_phys_active()
{

	return (ldxa(0, ASI_LSU_CTL_REG) & LSU_PW);
}

int
watch_virt_set_mask(vm_offset_t va, u_long mask)
{
	u_long lsucr;

	stxa_sync(AA_DMMU_VWPR, ASI_DMMU, va & (((2UL << 41) - 1) << 3));
	lsucr = ldxa(0, ASI_LSU_CTL_REG);
	lsucr = ((lsucr | LSU_VW) & ~LSU_VM_MASK) |
	    (mask << LSU_VM_SHIFT);
	stxa_sync(0, ASI_LSU_CTL_REG, lsucr);
	return (0);
}

int
watch_virt_set(vm_offset_t va, int sz)
{
	u_long off;

	off = (u_long)va & 7;
	/* Test for misaligned watch points. */
	if (off + sz > 8)
		return (-1);
	return (watch_virt_set_mask(va, ((1 << sz) - 1) << off));
}

vm_offset_t
watch_virt_get(int *bm)
{
	u_long va;
	u_long lsucr;
	
	if (!watch_virt_active())
		return (0);

	va = ldxa(AA_DMMU_VWPR, ASI_DMMU);
	lsucr = ldxa(0, ASI_LSU_CTL_REG);
	*bm = (lsucr & LSU_VM_MASK) >> LSU_VM_SHIFT;
	
	return ((vm_offset_t)va);
}

void
watch_virt_clear()
{
	stxa_sync(0, ASI_LSU_CTL_REG,
	    ldxa(0, ASI_LSU_CTL_REG) & ~LSU_VW);
}

int
watch_virt_active()
{

	return (ldxa(0, ASI_LSU_CTL_REG) & LSU_VW);
}

int
db_md_set_watchpoint(db_expr_t addr, db_expr_t size)
{
	int dummy;

	if (watch_virt_active()) {
		db_printf("Overwriting previously active watch point at "
		    "0x%lx\n", watch_virt_get(&dummy));
	}
	return (watch_virt_set(addr, size));
}

int
db_md_clr_watchpoint(db_expr_t addr, db_expr_t size)
{

	watch_virt_clear();
	return (0);
}

void
db_watch_print(vm_offset_t wp, int bm)
{
	int i;

	db_printf("\tat 0x%lx, active bytes: ", (u_long)wp);
	for (i = 0; i < 8; i++) {
		if ((bm & (1 << i)) != 0)
			db_printf("%d ", i);
	}
	if (bm == 0)
		db_printf("none");
	db_printf("\n");
}

void
db_md_list_watchpoints(void)
{
	vm_offset_t va;
	vm_paddr_t pa;
	int bm;

	db_printf("Physical address watchpoint:\n");
	if (watch_phys_active()) {
		pa = watch_phys_get(&bm);
		db_watch_print(pa, bm);
	} else
		db_printf("\tnot active.\n");
	db_printf("Virtual address watchpoint:\n");
	if (watch_virt_active()) {
		va = watch_virt_get(&bm);
		db_watch_print(va, bm);
	} else
		db_printf("\tnot active.\n");
}
