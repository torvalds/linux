/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2006 Yahoo!, Inc.
 * All rights reserved.
 * Written by: John Baldwin <jhb@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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

/*
 * Support for PCI Message Signalled Interrupts (MSI).  MSI interrupts on
 * x86 are basically APIC messages that the northbridge delivers directly
 * to the local APICs as if they had come from an I/O APIC.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_acpi.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/sx.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <x86/apicreg.h>
#include <machine/cputypes.h>
#include <machine/md_var.h>
#include <machine/frame.h>
#include <machine/intr_machdep.h>
#include <x86/apicvar.h>
#include <x86/iommu/iommu_intrmap.h>
#include <machine/specialreg.h>
#include <dev/pci/pcivar.h>

/* Fields in address for Intel MSI messages. */
#define	MSI_INTEL_ADDR_DEST		0x000ff000
#define	MSI_INTEL_ADDR_RH		0x00000008
# define MSI_INTEL_ADDR_RH_ON		0x00000008
# define MSI_INTEL_ADDR_RH_OFF		0x00000000
#define	MSI_INTEL_ADDR_DM		0x00000004
# define MSI_INTEL_ADDR_DM_PHYSICAL	0x00000000
# define MSI_INTEL_ADDR_DM_LOGICAL	0x00000004

/* Fields in data for Intel MSI messages. */
#define	MSI_INTEL_DATA_TRGRMOD		IOART_TRGRMOD	/* Trigger mode. */
# define MSI_INTEL_DATA_TRGREDG		IOART_TRGREDG
# define MSI_INTEL_DATA_TRGRLVL		IOART_TRGRLVL
#define	MSI_INTEL_DATA_LEVEL		0x00004000	/* Polarity. */
# define MSI_INTEL_DATA_DEASSERT	0x00000000
# define MSI_INTEL_DATA_ASSERT		0x00004000
#define	MSI_INTEL_DATA_DELMOD		IOART_DELMOD	/* Delivery mode. */
# define MSI_INTEL_DATA_DELFIXED	IOART_DELFIXED
# define MSI_INTEL_DATA_DELLOPRI	IOART_DELLOPRI
# define MSI_INTEL_DATA_DELSMI		IOART_DELSMI
# define MSI_INTEL_DATA_DELNMI		IOART_DELNMI
# define MSI_INTEL_DATA_DELINIT		IOART_DELINIT
# define MSI_INTEL_DATA_DELEXINT	IOART_DELEXINT
#define	MSI_INTEL_DATA_INTVEC		IOART_INTVEC	/* Interrupt vector. */

/*
 * Build Intel MSI message and data values from a source.  AMD64 systems
 * seem to be compatible, so we use the same function for both.
 */
#define	INTEL_ADDR(msi)							\
	(MSI_INTEL_ADDR_BASE | (msi)->msi_cpu << 12 |			\
	    MSI_INTEL_ADDR_RH_OFF | MSI_INTEL_ADDR_DM_PHYSICAL)
#define	INTEL_DATA(msi)							\
	(MSI_INTEL_DATA_TRGREDG | MSI_INTEL_DATA_DELFIXED | (msi)->msi_vector)

static MALLOC_DEFINE(M_MSI, "msi", "PCI MSI");

/*
 * MSI sources are bunched into groups.  This is because MSI forces
 * all of the messages to share the address and data registers and
 * thus certain properties (such as the local APIC ID target on x86).
 * Each group has a 'first' source that contains information global to
 * the group.  These fields are marked with (g) below.
 *
 * Note that local APIC ID is kind of special.  Each message will be
 * assigned an ID by the system; however, a group will use the ID from
 * the first message.
 *
 * For MSI-X, each message is isolated.
 */
struct msi_intsrc {
	struct intsrc msi_intsrc;
	device_t msi_dev;		/* Owning device. (g) */
	struct msi_intsrc *msi_first;	/* First source in group. */
	u_int msi_irq;			/* IRQ cookie. */
	u_int msi_msix;			/* MSI-X message. */
	u_int msi_vector:8;		/* IDT vector. */
	u_int msi_cpu;			/* Local APIC ID. (g) */
	u_int msi_count:8;		/* Messages in this group. (g) */
	u_int msi_maxcount:8;		/* Alignment for this group. (g) */
	u_int *msi_irqs;		/* Group's IRQ list. (g) */
	u_int msi_remap_cookie;
};

static void	msi_create_source(void);
static void	msi_enable_source(struct intsrc *isrc);
static void	msi_disable_source(struct intsrc *isrc, int eoi);
static void	msi_eoi_source(struct intsrc *isrc);
static void	msi_enable_intr(struct intsrc *isrc);
static void	msi_disable_intr(struct intsrc *isrc);
static int	msi_vector(struct intsrc *isrc);
static int	msi_source_pending(struct intsrc *isrc);
static int	msi_config_intr(struct intsrc *isrc, enum intr_trigger trig,
		    enum intr_polarity pol);
static int	msi_assign_cpu(struct intsrc *isrc, u_int apic_id);

struct pic msi_pic = {
	.pic_enable_source = msi_enable_source,
	.pic_disable_source = msi_disable_source,
	.pic_eoi_source = msi_eoi_source,
	.pic_enable_intr = msi_enable_intr,
	.pic_disable_intr = msi_disable_intr,
	.pic_vector = msi_vector,
	.pic_source_pending = msi_source_pending,
	.pic_suspend = NULL,
	.pic_resume = NULL,
	.pic_config_intr = msi_config_intr,
	.pic_assign_cpu = msi_assign_cpu,
	.pic_reprogram_pin = NULL,
};

u_int first_msi_irq;
SYSCTL_UINT(_machdep, OID_AUTO, first_msi_irq, CTLFLAG_RD, &first_msi_irq, 0,
    "Number of first IRQ reserved for MSI and MSI-X interrupts");

u_int num_msi_irqs = 512;
SYSCTL_UINT(_machdep, OID_AUTO, num_msi_irqs, CTLFLAG_RDTUN, &num_msi_irqs, 0,
    "Number of IRQs reserved for MSI and MSI-X interrupts");

#ifdef SMP
/**
 * Xen hypervisors prior to 4.6.0 do not properly handle updates to
 * enabled MSI-X table entries.  Allow migration of MSI-X interrupts
 * to be disabled via a tunable. Values have the following meaning:
 *
 * -1: automatic detection by FreeBSD
 *  0: enable migration
 *  1: disable migration
 */
int msix_disable_migration = -1;
SYSCTL_INT(_machdep, OID_AUTO, disable_msix_migration, CTLFLAG_RDTUN,
    &msix_disable_migration, 0,
    "Disable migration of MSI-X interrupts between CPUs");
#endif

static int msi_enabled;
static u_int msi_last_irq;
static struct mtx msi_lock;

static void
msi_enable_source(struct intsrc *isrc)
{
}

static void
msi_disable_source(struct intsrc *isrc, int eoi)
{

	if (eoi == PIC_EOI)
		lapic_eoi();
}

static void
msi_eoi_source(struct intsrc *isrc)
{

	lapic_eoi();
}

static void
msi_enable_intr(struct intsrc *isrc)
{
	struct msi_intsrc *msi = (struct msi_intsrc *)isrc;

	apic_enable_vector(msi->msi_cpu, msi->msi_vector);
}

static void
msi_disable_intr(struct intsrc *isrc)
{
	struct msi_intsrc *msi = (struct msi_intsrc *)isrc;

	apic_disable_vector(msi->msi_cpu, msi->msi_vector);
}

static int
msi_vector(struct intsrc *isrc)
{
	struct msi_intsrc *msi = (struct msi_intsrc *)isrc;

	return (msi->msi_irq);
}

static int
msi_source_pending(struct intsrc *isrc)
{

	return (0);
}

static int
msi_config_intr(struct intsrc *isrc, enum intr_trigger trig,
    enum intr_polarity pol)
{

	return (ENODEV);
}

static int
msi_assign_cpu(struct intsrc *isrc, u_int apic_id)
{
	struct msi_intsrc *sib, *msi = (struct msi_intsrc *)isrc;
	int old_vector;
	u_int old_id;
	int i, vector;

	/*
	 * Only allow CPUs to be assigned to the first message for an
	 * MSI group.
	 */
	if (msi->msi_first != msi)
		return (EINVAL);

#ifdef SMP
	if (msix_disable_migration && msi->msi_msix)
		return (EINVAL);
#endif

	/* Store information to free existing irq. */
	old_vector = msi->msi_vector;
	old_id = msi->msi_cpu;
	if (old_id == apic_id)
		return (0);

	/* Allocate IDT vectors on this cpu. */
	if (msi->msi_count > 1) {
		KASSERT(msi->msi_msix == 0, ("MSI-X message group"));
		vector = apic_alloc_vectors(apic_id, msi->msi_irqs,
		    msi->msi_count, msi->msi_maxcount);
	} else
		vector = apic_alloc_vector(apic_id, msi->msi_irq);
	if (vector == 0)
		return (ENOSPC);

	msi->msi_cpu = apic_id;
	msi->msi_vector = vector;
	if (msi->msi_intsrc.is_handlers > 0)
		apic_enable_vector(msi->msi_cpu, msi->msi_vector);
	if (bootverbose)
		printf("msi: Assigning %s IRQ %d to local APIC %u vector %u\n",
		    msi->msi_msix ? "MSI-X" : "MSI", msi->msi_irq,
		    msi->msi_cpu, msi->msi_vector);
	for (i = 1; i < msi->msi_count; i++) {
		sib = (struct msi_intsrc *)intr_lookup_source(msi->msi_irqs[i]);
		sib->msi_cpu = apic_id;
		sib->msi_vector = vector + i;
		if (sib->msi_intsrc.is_handlers > 0)
			apic_enable_vector(sib->msi_cpu, sib->msi_vector);
		if (bootverbose)
			printf(
		    "msi: Assigning MSI IRQ %d to local APIC %u vector %u\n",
			    sib->msi_irq, sib->msi_cpu, sib->msi_vector);
	}
	BUS_REMAP_INTR(device_get_parent(msi->msi_dev), msi->msi_dev,
	    msi->msi_irq);

	/*
	 * Free the old vector after the new one is established.  This is done
	 * to prevent races where we could miss an interrupt.
	 */
	if (msi->msi_intsrc.is_handlers > 0)
		apic_disable_vector(old_id, old_vector);
	apic_free_vector(old_id, old_vector, msi->msi_irq);
	for (i = 1; i < msi->msi_count; i++) {
		sib = (struct msi_intsrc *)intr_lookup_source(msi->msi_irqs[i]);
		if (sib->msi_intsrc.is_handlers > 0)
			apic_disable_vector(old_id, old_vector + i);
		apic_free_vector(old_id, old_vector + i, msi->msi_irqs[i]);
	}
	return (0);
}

void
msi_init(void)
{

	/* Check if we have a supported CPU. */
	switch (cpu_vendor_id) {
	case CPU_VENDOR_INTEL:
	case CPU_VENDOR_AMD:
		break;
	case CPU_VENDOR_CENTAUR:
		if (CPUID_TO_FAMILY(cpu_id) == 0x6 &&
		    CPUID_TO_MODEL(cpu_id) >= 0xf)
			break;
		/* FALLTHROUGH */
	default:
		return;
	}

#ifdef SMP
	if (msix_disable_migration == -1) {
		/* The default is to allow migration of MSI-X interrupts. */
		msix_disable_migration = 0;
	}
#endif

	if (num_msi_irqs == 0)
		return;

	first_msi_irq = num_io_irqs;
	if (num_msi_irqs > UINT_MAX - first_msi_irq)
		panic("num_msi_irqs too high");
	num_io_irqs = first_msi_irq + num_msi_irqs;

	msi_enabled = 1;
	intr_register_pic(&msi_pic);
	mtx_init(&msi_lock, "msi", NULL, MTX_DEF);
}

static void
msi_create_source(void)
{
	struct msi_intsrc *msi;
	u_int irq;

	mtx_lock(&msi_lock);
	if (msi_last_irq >= num_msi_irqs) {
		mtx_unlock(&msi_lock);
		return;
	}
	irq = msi_last_irq + first_msi_irq;
	msi_last_irq++;
	mtx_unlock(&msi_lock);

	msi = malloc(sizeof(struct msi_intsrc), M_MSI, M_WAITOK | M_ZERO);
	msi->msi_intsrc.is_pic = &msi_pic;
	msi->msi_irq = irq;
	intr_register_source(&msi->msi_intsrc);
	nexus_add_irq(irq);
}

/*
 * Try to allocate 'count' interrupt sources with contiguous IDT values.
 */
int
msi_alloc(device_t dev, int count, int maxcount, int *irqs)
{
	struct msi_intsrc *msi, *fsrc;
	u_int cpu, domain, *mirqs;
	int cnt, i, vector;
#ifdef ACPI_DMAR
	u_int cookies[count];
	int error;
#endif

	if (!msi_enabled)
		return (ENXIO);

	if (bus_get_domain(dev, &domain) != 0)
		domain = 0;

	if (count > 1)
		mirqs = malloc(count * sizeof(*mirqs), M_MSI, M_WAITOK);
	else
		mirqs = NULL;
again:
	mtx_lock(&msi_lock);

	/* Try to find 'count' free IRQs. */
	cnt = 0;
	for (i = first_msi_irq; i < first_msi_irq + num_msi_irqs; i++) {
		msi = (struct msi_intsrc *)intr_lookup_source(i);

		/* End of allocated sources, so break. */
		if (msi == NULL)
			break;

		/* If this is a free one, save its IRQ in the array. */
		if (msi->msi_dev == NULL) {
			irqs[cnt] = i;
			cnt++;
			if (cnt == count)
				break;
		}
	}

	/* Do we need to create some new sources? */
	if (cnt < count) {
		/* If we would exceed the max, give up. */
		if (i + (count - cnt) > first_msi_irq + num_msi_irqs) {
			mtx_unlock(&msi_lock);
			free(mirqs, M_MSI);
			return (ENXIO);
		}
		mtx_unlock(&msi_lock);

		/* We need count - cnt more sources. */
		while (cnt < count) {
			msi_create_source();
			cnt++;
		}
		goto again;
	}

	/* Ok, we now have the IRQs allocated. */
	KASSERT(cnt == count, ("count mismatch"));

	/* Allocate 'count' IDT vectors. */
	cpu = intr_next_cpu(domain);
	vector = apic_alloc_vectors(cpu, irqs, count, maxcount);
	if (vector == 0) {
		mtx_unlock(&msi_lock);
		free(mirqs, M_MSI);
		return (ENOSPC);
	}

#ifdef ACPI_DMAR
	mtx_unlock(&msi_lock);
	error = iommu_alloc_msi_intr(dev, cookies, count);
	mtx_lock(&msi_lock);
	if (error == EOPNOTSUPP)
		error = 0;
	if (error != 0) {
		for (i = 0; i < count; i++)
			apic_free_vector(cpu, vector + i, irqs[i]);
		free(mirqs, M_MSI);
		return (error);
	}
	for (i = 0; i < count; i++) {
		msi = (struct msi_intsrc *)intr_lookup_source(irqs[i]);
		msi->msi_remap_cookie = cookies[i];
	}
#endif

	/* Assign IDT vectors and make these messages owned by 'dev'. */
	fsrc = (struct msi_intsrc *)intr_lookup_source(irqs[0]);
	for (i = 0; i < count; i++) {
		msi = (struct msi_intsrc *)intr_lookup_source(irqs[i]);
		msi->msi_cpu = cpu;
		msi->msi_dev = dev;
		msi->msi_vector = vector + i;
		if (bootverbose)
			printf(
		    "msi: routing MSI IRQ %d to local APIC %u vector %u\n",
			    msi->msi_irq, msi->msi_cpu, msi->msi_vector);
		msi->msi_first = fsrc;
		KASSERT(msi->msi_intsrc.is_handlers == 0,
		    ("dead MSI has handlers"));
	}
	fsrc->msi_count = count;
	fsrc->msi_maxcount = maxcount;
	if (count > 1)
		bcopy(irqs, mirqs, count * sizeof(*mirqs));
	fsrc->msi_irqs = mirqs;
	mtx_unlock(&msi_lock);
	return (0);
}

int
msi_release(int *irqs, int count)
{
	struct msi_intsrc *msi, *first;
	int i;

	mtx_lock(&msi_lock);
	first = (struct msi_intsrc *)intr_lookup_source(irqs[0]);
	if (first == NULL) {
		mtx_unlock(&msi_lock);
		return (ENOENT);
	}

	/* Make sure this isn't an MSI-X message. */
	if (first->msi_msix) {
		mtx_unlock(&msi_lock);
		return (EINVAL);
	}

	/* Make sure this message is allocated to a group. */
	if (first->msi_first == NULL) {
		mtx_unlock(&msi_lock);
		return (ENXIO);
	}

	/*
	 * Make sure this is the start of a group and that we are releasing
	 * the entire group.
	 */
	if (first->msi_first != first || first->msi_count != count) {
		mtx_unlock(&msi_lock);
		return (EINVAL);
	}
	KASSERT(first->msi_dev != NULL, ("unowned group"));

	/* Clear all the extra messages in the group. */
	for (i = 1; i < count; i++) {
		msi = (struct msi_intsrc *)intr_lookup_source(irqs[i]);
		KASSERT(msi->msi_first == first, ("message not in group"));
		KASSERT(msi->msi_dev == first->msi_dev, ("owner mismatch"));
#ifdef ACPI_DMAR
		iommu_unmap_msi_intr(first->msi_dev, msi->msi_remap_cookie);
#endif
		msi->msi_first = NULL;
		msi->msi_dev = NULL;
		apic_free_vector(msi->msi_cpu, msi->msi_vector, msi->msi_irq);
		msi->msi_vector = 0;
	}

	/* Clear out the first message. */
#ifdef ACPI_DMAR
	mtx_unlock(&msi_lock);
	iommu_unmap_msi_intr(first->msi_dev, first->msi_remap_cookie);
	mtx_lock(&msi_lock);
#endif
	first->msi_first = NULL;
	first->msi_dev = NULL;
	apic_free_vector(first->msi_cpu, first->msi_vector, first->msi_irq);
	first->msi_vector = 0;
	first->msi_count = 0;
	first->msi_maxcount = 0;
	free(first->msi_irqs, M_MSI);
	first->msi_irqs = NULL;

	mtx_unlock(&msi_lock);
	return (0);
}

int
msi_map(int irq, uint64_t *addr, uint32_t *data)
{
	struct msi_intsrc *msi;
	int error;
#ifdef ACPI_DMAR
	struct msi_intsrc *msi1;
	int i, k;
#endif

	mtx_lock(&msi_lock);
	msi = (struct msi_intsrc *)intr_lookup_source(irq);
	if (msi == NULL) {
		mtx_unlock(&msi_lock);
		return (ENOENT);
	}

	/* Make sure this message is allocated to a device. */
	if (msi->msi_dev == NULL) {
		mtx_unlock(&msi_lock);
		return (ENXIO);
	}

	/*
	 * If this message isn't an MSI-X message, make sure it's part
	 * of a group, and switch to the first message in the
	 * group.
	 */
	if (!msi->msi_msix) {
		if (msi->msi_first == NULL) {
			mtx_unlock(&msi_lock);
			return (ENXIO);
		}
		msi = msi->msi_first;
	}

#ifdef ACPI_DMAR
	if (!msi->msi_msix) {
		for (k = msi->msi_count - 1, i = first_msi_irq; k > 0 &&
		    i < first_msi_irq + num_msi_irqs; i++) {
			if (i == msi->msi_irq)
				continue;
			msi1 = (struct msi_intsrc *)intr_lookup_source(i);
			if (!msi1->msi_msix && msi1->msi_first == msi) {
				mtx_unlock(&msi_lock);
				iommu_map_msi_intr(msi1->msi_dev,
				    msi1->msi_cpu, msi1->msi_vector,
				    msi1->msi_remap_cookie, NULL, NULL);
				k--;
				mtx_lock(&msi_lock);
			}
		}
	}
	mtx_unlock(&msi_lock);
	error = iommu_map_msi_intr(msi->msi_dev, msi->msi_cpu,
	    msi->msi_vector, msi->msi_remap_cookie, addr, data);
#else
	mtx_unlock(&msi_lock);
	error = EOPNOTSUPP;
#endif
	if (error == EOPNOTSUPP) {
		*addr = INTEL_ADDR(msi);
		*data = INTEL_DATA(msi);
		error = 0;
	}
	return (error);
}

int
msix_alloc(device_t dev, int *irq)
{
	struct msi_intsrc *msi;
	u_int cpu, domain;
	int i, vector;
#ifdef ACPI_DMAR
	u_int cookie;
	int error;
#endif

	if (!msi_enabled)
		return (ENXIO);

	if (bus_get_domain(dev, &domain) != 0)
		domain = 0;

again:
	mtx_lock(&msi_lock);

	/* Find a free IRQ. */
	for (i = first_msi_irq; i < first_msi_irq + num_msi_irqs; i++) {
		msi = (struct msi_intsrc *)intr_lookup_source(i);

		/* End of allocated sources, so break. */
		if (msi == NULL)
			break;

		/* Stop at the first free source. */
		if (msi->msi_dev == NULL)
			break;
	}

	/* Are all IRQs in use? */
	if (i == first_msi_irq + num_msi_irqs) {
		mtx_unlock(&msi_lock);
		return (ENXIO);
	}

	/* Do we need to create a new source? */
	if (msi == NULL) {
		mtx_unlock(&msi_lock);

		/* Create a new source. */
		msi_create_source();
		goto again;
	}

	/* Allocate an IDT vector. */
	cpu = intr_next_cpu(domain);
	vector = apic_alloc_vector(cpu, i);
	if (vector == 0) {
		mtx_unlock(&msi_lock);
		return (ENOSPC);
	}

	msi->msi_dev = dev;
#ifdef ACPI_DMAR
	mtx_unlock(&msi_lock);
	error = iommu_alloc_msi_intr(dev, &cookie, 1);
	mtx_lock(&msi_lock);
	if (error == EOPNOTSUPP)
		error = 0;
	if (error != 0) {
		msi->msi_dev = NULL;
		apic_free_vector(cpu, vector, i);
		return (error);
	}
	msi->msi_remap_cookie = cookie;
#endif

	if (bootverbose)
		printf("msi: routing MSI-X IRQ %d to local APIC %u vector %u\n",
		    msi->msi_irq, cpu, vector);

	/* Setup source. */
	msi->msi_cpu = cpu;
	msi->msi_first = msi;
	msi->msi_vector = vector;
	msi->msi_msix = 1;
	msi->msi_count = 1;
	msi->msi_maxcount = 1;
	msi->msi_irqs = NULL;

	KASSERT(msi->msi_intsrc.is_handlers == 0, ("dead MSI-X has handlers"));
	mtx_unlock(&msi_lock);

	*irq = i;
	return (0);
}

int
msix_release(int irq)
{
	struct msi_intsrc *msi;

	mtx_lock(&msi_lock);
	msi = (struct msi_intsrc *)intr_lookup_source(irq);
	if (msi == NULL) {
		mtx_unlock(&msi_lock);
		return (ENOENT);
	}

	/* Make sure this is an MSI-X message. */
	if (!msi->msi_msix) {
		mtx_unlock(&msi_lock);
		return (EINVAL);
	}

	KASSERT(msi->msi_dev != NULL, ("unowned message"));

	/* Clear out the message. */
#ifdef ACPI_DMAR
	mtx_unlock(&msi_lock);
	iommu_unmap_msi_intr(msi->msi_dev, msi->msi_remap_cookie);
	mtx_lock(&msi_lock);
#endif
	msi->msi_first = NULL;
	msi->msi_dev = NULL;
	apic_free_vector(msi->msi_cpu, msi->msi_vector, msi->msi_irq);
	msi->msi_vector = 0;
	msi->msi_msix = 0;
	msi->msi_count = 0;
	msi->msi_maxcount = 0;

	mtx_unlock(&msi_lock);
	return (0);
}
