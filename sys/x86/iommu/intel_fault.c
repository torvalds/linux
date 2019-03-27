/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 The FreeBSD Foundation
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

#include "opt_acpi.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/memdesc.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/taskqueue.h>
#include <sys/tree.h>
#include <sys/vmem.h>
#include <machine/bus.h>
#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>
#include <dev/acpica/acpivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <x86/include/busdma_impl.h>
#include <x86/iommu/intel_reg.h>
#include <x86/iommu/busdma_dmar.h>
#include <x86/iommu/intel_dmar.h>

/*
 * Fault interrupt handling for DMARs.  If advanced fault logging is
 * not implemented by hardware, the code emulates it.  Fast interrupt
 * handler flushes the fault registers into circular buffer at
 * unit->fault_log, and schedules a task.
 *
 * The fast handler is used since faults usually come in bursts, and
 * number of fault log registers is limited, e.g. down to one for 5400
 * MCH.  We are trying to reduce the latency for clearing the fault
 * register file.  The task is usually long-running, since printf() is
 * slow, but this is not problematic because bursts are rare.
 *
 * For the same reason, each translation unit task is executed in its
 * own thread.
 *
 * XXXKIB It seems there is no hardware available which implements
 * advanced fault logging, so the code to handle AFL is not written.
 */

static int
dmar_fault_next(struct dmar_unit *unit, int faultp)
{

	faultp += 2;
	if (faultp == unit->fault_log_size)
		faultp = 0;
	return (faultp);
}

static void
dmar_fault_intr_clear(struct dmar_unit *unit, uint32_t fsts)
{
	uint32_t clear;

	clear = 0;
	if ((fsts & DMAR_FSTS_ITE) != 0) {
		printf("DMAR%d: Invalidation timed out\n", unit->unit);
		clear |= DMAR_FSTS_ITE;
	}
	if ((fsts & DMAR_FSTS_ICE) != 0) {
		printf("DMAR%d: Invalidation completion error\n",
		    unit->unit);
		clear |= DMAR_FSTS_ICE;
	}
	if ((fsts & DMAR_FSTS_IQE) != 0) {
		printf("DMAR%d: Invalidation queue error\n",
		    unit->unit);
		clear |= DMAR_FSTS_IQE;
	}
	if ((fsts & DMAR_FSTS_APF) != 0) {
		printf("DMAR%d: Advanced pending fault\n", unit->unit);
		clear |= DMAR_FSTS_APF;
	}
	if ((fsts & DMAR_FSTS_AFO) != 0) {
		printf("DMAR%d: Advanced fault overflow\n", unit->unit);
		clear |= DMAR_FSTS_AFO;
	}
	if (clear != 0)
		dmar_write4(unit, DMAR_FSTS_REG, clear);
}

int
dmar_fault_intr(void *arg)
{
	struct dmar_unit *unit;
	uint64_t fault_rec[2];
	uint32_t fsts;
	int fri, frir, faultp;
	bool enqueue;

	unit = arg;
	enqueue = false;
	fsts = dmar_read4(unit, DMAR_FSTS_REG);
	dmar_fault_intr_clear(unit, fsts);

	if ((fsts & DMAR_FSTS_PPF) == 0)
		goto done;

	fri = DMAR_FSTS_FRI(fsts);
	for (;;) {
		frir = (DMAR_CAP_FRO(unit->hw_cap) + fri) * 16;
		fault_rec[1] = dmar_read8(unit, frir + 8);
		if ((fault_rec[1] & DMAR_FRCD2_F) == 0)
			break;
		fault_rec[0] = dmar_read8(unit, frir);
		dmar_write4(unit, frir + 12, DMAR_FRCD2_F32);
		DMAR_FAULT_LOCK(unit);
		faultp = unit->fault_log_head;
		if (dmar_fault_next(unit, faultp) == unit->fault_log_tail) {
			/* XXXKIB log overflow */
		} else {
			unit->fault_log[faultp] = fault_rec[0];
			unit->fault_log[faultp + 1] = fault_rec[1];
			unit->fault_log_head = dmar_fault_next(unit, faultp);
			enqueue = true;
		}
		DMAR_FAULT_UNLOCK(unit);
		fri += 1;
		if (fri >= DMAR_CAP_NFR(unit->hw_cap))
			fri = 0;
	}

done:
	/*
	 * On SandyBridge, due to errata BJ124, IvyBridge errata
	 * BV100, and Haswell errata HSD40, "Spurious Intel VT-d
	 * Interrupts May Occur When the PFO Bit is Set".  Handle the
	 * cases by clearing overflow bit even if no fault is
	 * reported.
	 *
	 * On IvyBridge, errata BV30 states that clearing clear
	 * DMAR_FRCD2_F bit in the fault register causes spurious
	 * interrupt.  Do nothing.
	 *
	 */
	if ((fsts & DMAR_FSTS_PFO) != 0) {
		printf("DMAR%d: Fault Overflow\n", unit->unit);
		dmar_write4(unit, DMAR_FSTS_REG, DMAR_FSTS_PFO);
	}

	if (enqueue) {
		taskqueue_enqueue(unit->fault_taskqueue,
		    &unit->fault_task);
	}
	return (FILTER_HANDLED);
}

static void
dmar_fault_task(void *arg, int pending __unused)
{
	struct dmar_unit *unit;
	struct dmar_ctx *ctx;
	uint64_t fault_rec[2];
	int sid, bus, slot, func, faultp;

	unit = arg;
	DMAR_FAULT_LOCK(unit);
	for (;;) {
		faultp = unit->fault_log_tail;
		if (faultp == unit->fault_log_head)
			break;

		fault_rec[0] = unit->fault_log[faultp];
		fault_rec[1] = unit->fault_log[faultp + 1];
		unit->fault_log_tail = dmar_fault_next(unit, faultp);
		DMAR_FAULT_UNLOCK(unit);

		sid = DMAR_FRCD2_SID(fault_rec[1]);
		printf("DMAR%d: ", unit->unit);
		DMAR_LOCK(unit);
		ctx = dmar_find_ctx_locked(unit, sid);
		if (ctx == NULL) {
			printf("<unknown dev>:");

			/*
			 * Note that the slot and function will not be correct
			 * if ARI is in use, but without a ctx entry we have
			 * no way of knowing whether ARI is in use or not.
			 */
			bus = PCI_RID2BUS(sid);
			slot = PCI_RID2SLOT(sid);
			func = PCI_RID2FUNC(sid);
		} else {
			ctx->flags |= DMAR_CTX_FAULTED;
			ctx->last_fault_rec[0] = fault_rec[0];
			ctx->last_fault_rec[1] = fault_rec[1];
			device_print_prettyname(ctx->ctx_tag.owner);
			bus = pci_get_bus(ctx->ctx_tag.owner);
			slot = pci_get_slot(ctx->ctx_tag.owner);
			func = pci_get_function(ctx->ctx_tag.owner);
		}
		DMAR_UNLOCK(unit);
		printf(
		    "pci%d:%d:%d sid %x fault acc %x adt 0x%x reason 0x%x "
		    "addr %jx\n",
		    bus, slot, func, sid, DMAR_FRCD2_T(fault_rec[1]),
		    DMAR_FRCD2_AT(fault_rec[1]), DMAR_FRCD2_FR(fault_rec[1]),
		    (uintmax_t)fault_rec[0]);
		DMAR_FAULT_LOCK(unit);
	}
	DMAR_FAULT_UNLOCK(unit);
}

static void
dmar_clear_faults(struct dmar_unit *unit)
{
	uint32_t frec, frir, fsts;
	int i;

	for (i = 0; i < DMAR_CAP_NFR(unit->hw_cap); i++) {
		frir = (DMAR_CAP_FRO(unit->hw_cap) + i) * 16;
		frec = dmar_read4(unit, frir + 12);
		if ((frec & DMAR_FRCD2_F32) == 0)
			continue;
		dmar_write4(unit, frir + 12, DMAR_FRCD2_F32);
	}
	fsts = dmar_read4(unit, DMAR_FSTS_REG);
	dmar_write4(unit, DMAR_FSTS_REG, fsts);
}

int
dmar_init_fault_log(struct dmar_unit *unit)
{

	mtx_init(&unit->fault_lock, "dmarflt", NULL, MTX_SPIN);
	unit->fault_log_size = 256; /* 128 fault log entries */
	TUNABLE_INT_FETCH("hw.dmar.fault_log_size", &unit->fault_log_size);
	if (unit->fault_log_size % 2 != 0)
		panic("hw.dmar_fault_log_size must be even");
	unit->fault_log = malloc(sizeof(uint64_t) * unit->fault_log_size,
	    M_DEVBUF, M_WAITOK | M_ZERO);

	TASK_INIT(&unit->fault_task, 0, dmar_fault_task, unit);
	unit->fault_taskqueue = taskqueue_create_fast("dmarff", M_WAITOK,
	    taskqueue_thread_enqueue, &unit->fault_taskqueue);
	taskqueue_start_threads(&unit->fault_taskqueue, 1, PI_AV,
	    "dmar%d fault taskq", unit->unit);

	DMAR_LOCK(unit);
	dmar_disable_fault_intr(unit);
	dmar_clear_faults(unit);
	dmar_enable_fault_intr(unit);
	DMAR_UNLOCK(unit);

	return (0);
}

void
dmar_fini_fault_log(struct dmar_unit *unit)
{

	DMAR_LOCK(unit);
	dmar_disable_fault_intr(unit);
	DMAR_UNLOCK(unit);

	if (unit->fault_taskqueue == NULL)
		return;

	taskqueue_drain(unit->fault_taskqueue, &unit->fault_task);
	taskqueue_free(unit->fault_taskqueue);
	unit->fault_taskqueue = NULL;
	mtx_destroy(&unit->fault_lock);

	free(unit->fault_log, M_DEVBUF);
	unit->fault_log = NULL;
	unit->fault_log_head = unit->fault_log_tail = 0;
}

void
dmar_enable_fault_intr(struct dmar_unit *unit)
{
	uint32_t fectl;

	DMAR_ASSERT_LOCKED(unit);
	fectl = dmar_read4(unit, DMAR_FECTL_REG);
	fectl &= ~DMAR_FECTL_IM;
	dmar_write4(unit, DMAR_FECTL_REG, fectl);
}

void
dmar_disable_fault_intr(struct dmar_unit *unit)
{
	uint32_t fectl;

	DMAR_ASSERT_LOCKED(unit);
	fectl = dmar_read4(unit, DMAR_FECTL_REG);
	dmar_write4(unit, DMAR_FECTL_REG, fectl | DMAR_FECTL_IM);
}
