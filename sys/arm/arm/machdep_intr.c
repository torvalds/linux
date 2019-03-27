/*-
 * Copyright (c) 2015-2016 Svatopluk Kraus
 * Copyright (c) 2015-2016 Michal Meloun
 * All rights reserved.
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
 */

#include "opt_platform.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/syslog.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/bus.h>
#include <sys/interrupt.h>
#include <sys/conf.h>
#include <sys/pmc.h>
#include <sys/pmckern.h>
#include <sys/smp.h>

#include <machine/atomic.h>
#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/cpu.h>
#include <machine/smp.h>

#ifdef INTRNG
#include "pic_if.h"

#ifdef SMP
#define INTR_IPI_NAMELEN	(MAXCOMLEN + 1)

struct intr_ipi {
	intr_ipi_handler_t *	ii_handler;
	void *			ii_handler_arg;
	intr_ipi_send_t *	ii_send;
	void *			ii_send_arg;
	char			ii_name[INTR_IPI_NAMELEN];
	u_long *		ii_count;
};

static struct intr_ipi ipi_sources[INTR_IPI_COUNT];
#endif
#endif

/*
 * arm_irq_memory_barrier()
 *
 * Ensure all writes to device memory have reached devices before proceeding.
 *
 * This is intended to be called from the post-filter and post-thread routines
 * of an interrupt controller implementation.  A peripheral device driver should
 * use bus_space_barrier() if it needs to ensure a write has reached the
 * hardware for some reason other than clearing interrupt conditions.
 *
 * The need for this function arises from the ARM weak memory ordering model.
 * Writes to locations mapped with the Device attribute bypass any caches, but
 * are buffered.  Multiple writes to the same device will be observed by that
 * device in the order issued by the cpu.  Writes to different devices may
 * appear at those devices in a different order than issued by the cpu.  That
 * is, if the cpu writes to device A then device B, the write to device B could
 * complete before the write to device A.
 *
 * Consider a typical device interrupt handler which services the interrupt and
 * writes to a device status-acknowledge register to clear the interrupt before
 * returning.  That write is posted to the L2 controller which "immediately"
 * places it in a store buffer and automatically drains that buffer.  This can
 * be less immediate than you'd think... There may be no free slots in the store
 * buffers, so an existing buffer has to be drained first to make room.  The
 * target bus may be busy with other traffic (such as DMA for various devices),
 * delaying the drain of the store buffer for some indeterminate time.  While
 * all this delay is happening, execution proceeds on the CPU, unwinding its way
 * out of the interrupt call stack to the point where the interrupt driver code
 * is ready to EOI and unmask the interrupt.  The interrupt controller may be
 * accessed via a faster bus than the hardware whose handler just ran; the write
 * to unmask and EOI the interrupt may complete quickly while the device write
 * to ack and clear the interrupt source is still lingering in a store buffer
 * waiting for access to a slower bus.  With the interrupt unmasked at the
 * interrupt controller but still active at the device, as soon as interrupts
 * are enabled on the core the device re-interrupts immediately: now you've got
 * a spurious interrupt on your hands.
 *
 * The right way to fix this problem is for every device driver to use the
 * proper bus_space_barrier() calls in its interrupt handler.  For ARM a single
 * barrier call at the end of the handler would work.  This would have to be
 * done to every driver in the system, not just arm-specific drivers.
 *
 * Another potential fix is to map all device memory as Strongly-Ordered rather
 * than Device memory, which takes the store buffers out of the picture.  This
 * has a pretty big impact on overall system performance, because each strongly
 * ordered memory access causes all L2 store buffers to be drained.
 *
 * A compromise solution is to have the interrupt controller implementation call
 * this function to establish a barrier between writes to the interrupt-source
 * device and writes to the interrupt controller device.
 *
 * This takes the interrupt number as an argument, and currently doesn't use it.
 * The plan is that maybe some day there is a way to flag certain interrupts as
 * "memory barrier safe" and we can avoid this overhead with them.
 */
void
arm_irq_memory_barrier(uintptr_t irq)
{

	dsb();
	cpu_l2cache_drain_writebuf();
}

#ifdef INTRNG
#ifdef SMP
static inline struct intr_ipi *
intr_ipi_lookup(u_int ipi)
{

	if (ipi >= INTR_IPI_COUNT)
		panic("%s: no such IPI %u", __func__, ipi);

	return (&ipi_sources[ipi]);
}

void
intr_ipi_dispatch(u_int ipi, struct trapframe *tf)
{
	void *arg;
	struct intr_ipi *ii;

	ii = intr_ipi_lookup(ipi);
	if (ii->ii_count == NULL)
		panic("%s: not setup IPI %u", __func__, ipi);

	intr_ipi_increment_count(ii->ii_count, PCPU_GET(cpuid));

	/*
	 * Supply ipi filter with trapframe argument
	 * if none is registered.
	 */
	arg = ii->ii_handler_arg != NULL ? ii->ii_handler_arg : tf;
	ii->ii_handler(arg);
}

void
intr_ipi_send(cpuset_t cpus, u_int ipi)
{
	struct intr_ipi *ii;

	ii = intr_ipi_lookup(ipi);
	if (ii->ii_count == NULL)
		panic("%s: not setup IPI %u", __func__, ipi);

	ii->ii_send(ii->ii_send_arg, cpus, ipi);
}

void
intr_ipi_setup(u_int ipi, const char *name, intr_ipi_handler_t *hand,
    void *h_arg, intr_ipi_send_t *send, void *s_arg)
{
	struct intr_ipi *ii;

	ii = intr_ipi_lookup(ipi);

	KASSERT(hand != NULL, ("%s: ipi %u no handler", __func__, ipi));
	KASSERT(send != NULL, ("%s: ipi %u no sender", __func__, ipi));
	KASSERT(ii->ii_count == NULL, ("%s: ipi %u reused", __func__, ipi));

	ii->ii_handler = hand;
	ii->ii_handler_arg = h_arg;
	ii->ii_send = send;
	ii->ii_send_arg = s_arg;
	strlcpy(ii->ii_name, name, INTR_IPI_NAMELEN);
	ii->ii_count = intr_ipi_setup_counters(name);
}

/*
 *  Send IPI thru interrupt controller.
 */
static void
pic_ipi_send(void *arg, cpuset_t cpus, u_int ipi)
{

	KASSERT(intr_irq_root_dev != NULL, ("%s: no root attached", __func__));
	PIC_IPI_SEND(intr_irq_root_dev, arg, cpus, ipi);
}

/*
 *  Setup IPI handler on interrupt controller.
 *
 *  Not SMP coherent.
 */
int
intr_pic_ipi_setup(u_int ipi, const char *name, intr_ipi_handler_t *hand,
    void *arg)
{
	int error;
	struct intr_irqsrc *isrc;

	KASSERT(intr_irq_root_dev != NULL, ("%s: no root attached", __func__));

	error = PIC_IPI_SETUP(intr_irq_root_dev, ipi, &isrc);
	if (error != 0)
		return (error);

	isrc->isrc_handlers++;
	intr_ipi_setup(ipi, name, hand, arg, pic_ipi_send, isrc);
	return (0);
}
#endif
#endif
