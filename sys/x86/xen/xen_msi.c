/*
 * Copyright (c) 2014 Roger Pau Monné <roger.pau@citrix.com>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS AS IS'' AND
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/sx.h>
#include <sys/systm.h>
#include <x86/apicreg.h>
#include <machine/cputypes.h>
#include <machine/md_var.h>
#include <machine/frame.h>
#include <machine/intr_machdep.h>
#include <x86/apicvar.h>
#include <machine/specialreg.h>
#include <dev/pci/pcivar.h>

#include <xen/xen-os.h>
#include <xen/xen_intr.h>
#include <xen/xen_msi.h>

static struct mtx msi_lock;
static u_int msi_last_irq;

void
xen_msi_init(void)
{

	MPASS(num_io_irqs > 0);
	first_msi_irq = num_io_irqs;
	if (num_msi_irqs > UINT_MAX - first_msi_irq)
		panic("num_msi_irqs too high");
	num_io_irqs = first_msi_irq + num_msi_irqs;

	mtx_init(&msi_lock, "msi", NULL, MTX_DEF);
}

/*
 * Try to allocate 'count' interrupt sources with contiguous IDT values.
 */
int
xen_msi_alloc(device_t dev, int count, int maxcount, int *irqs)
{
	int i, ret = 0;

	mtx_lock(&msi_lock);

	/* If we would exceed the max, give up. */
	if (msi_last_irq + count > num_msi_irqs) {
		mtx_unlock(&msi_lock);
		return (ENXIO);
	}

	/* Allocate MSI vectors */
	for (i = 0; i < count; i++)
		irqs[i] = first_msi_irq + msi_last_irq++;

	mtx_unlock(&msi_lock);

	ret = xen_register_msi(dev, irqs[0], count);
	if (ret != 0)
		return (ret);

	for (i = 0; i < count; i++)
		nexus_add_irq(irqs[i]);

	return (0);
}

int
xen_msi_release(int *irqs, int count)
{
	int i, ret;

	for (i = 0; i < count; i++) {
		ret = xen_release_msi(irqs[i]);
		if (ret != 0)
			return (ret);
	}

	return (0);
}

int
xen_msi_map(int irq, uint64_t *addr, uint32_t *data)
{

	return (0);
}

int
xen_msix_alloc(device_t dev, int *irq)
{

	return (ENXIO);
}

int
xen_msix_release(int irq)
{

	return (ENOENT);
}
