// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Multiplex several virtual IPIs over a single HW IPI.
 *
 * Copyright The Asahi Linux Contributors
 * Copyright (c) 2022 Ventana Micro Systems Inc.
 */

#define pr_fmt(fmt) "ipi-mux: " fmt
#include <linux/cpu.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/jump_label.h>
#include <linux/percpu.h>
#include <linux/smp.h>

struct ipi_mux_cpu {
	atomic_t			enable;
	atomic_t			bits;
};

static struct ipi_mux_cpu __percpu *ipi_mux_pcpu;
static struct irq_domain *ipi_mux_domain;
static void (*ipi_mux_send)(unsigned int cpu);

static void ipi_mux_mask(struct irq_data *d)
{
	struct ipi_mux_cpu *icpu = this_cpu_ptr(ipi_mux_pcpu);

	atomic_andnot(BIT(irqd_to_hwirq(d)), &icpu->enable);
}

static void ipi_mux_unmask(struct irq_data *d)
{
	struct ipi_mux_cpu *icpu = this_cpu_ptr(ipi_mux_pcpu);
	u32 ibit = BIT(irqd_to_hwirq(d));

	atomic_or(ibit, &icpu->enable);

	/*
	 * The atomic_or() above must complete before the atomic_read()
	 * below to avoid racing ipi_mux_send_mask().
	 */
	smp_mb__after_atomic();

	/* If a pending IPI was unmasked, raise a parent IPI immediately. */
	if (atomic_read(&icpu->bits) & ibit)
		ipi_mux_send(smp_processor_id());
}

static void ipi_mux_send_mask(struct irq_data *d, const struct cpumask *mask)
{
	struct ipi_mux_cpu *icpu = this_cpu_ptr(ipi_mux_pcpu);
	u32 ibit = BIT(irqd_to_hwirq(d));
	unsigned long pending;
	int cpu;

	for_each_cpu(cpu, mask) {
		icpu = per_cpu_ptr(ipi_mux_pcpu, cpu);

		/*
		 * This sequence is the mirror of the one in ipi_mux_unmask();
		 * see the comment there. Additionally, release semantics
		 * ensure that the vIPI flag set is ordered after any shared
		 * memory accesses that precede it. This therefore also pairs
		 * with the atomic_fetch_andnot in ipi_mux_process().
		 */
		pending = atomic_fetch_or_release(ibit, &icpu->bits);

		/*
		 * The atomic_fetch_or_release() above must complete
		 * before the atomic_read() below to avoid racing with
		 * ipi_mux_unmask().
		 */
		smp_mb__after_atomic();

		/*
		 * The flag writes must complete before the physical IPI is
		 * issued to another CPU. This is implied by the control
		 * dependency on the result of atomic_read() below, which is
		 * itself already ordered after the vIPI flag write.
		 */
		if (!(pending & ibit) && (atomic_read(&icpu->enable) & ibit))
			ipi_mux_send(cpu);
	}
}

static const struct irq_chip ipi_mux_chip = {
	.name		= "IPI Mux",
	.irq_mask	= ipi_mux_mask,
	.irq_unmask	= ipi_mux_unmask,
	.ipi_send_mask	= ipi_mux_send_mask,
};

static int ipi_mux_domain_alloc(struct irq_domain *d, unsigned int virq,
				unsigned int nr_irqs, void *arg)
{
	int i;

	for (i = 0; i < nr_irqs; i++) {
		irq_set_percpu_devid(virq + i);
		irq_domain_set_info(d, virq + i, i, &ipi_mux_chip, NULL,
				    handle_percpu_devid_irq, NULL, NULL);
	}

	return 0;
}

static const struct irq_domain_ops ipi_mux_domain_ops = {
	.alloc		= ipi_mux_domain_alloc,
	.free		= irq_domain_free_irqs_top,
};

/**
 * ipi_mux_process - Process multiplexed virtual IPIs
 */
void ipi_mux_process(void)
{
	struct ipi_mux_cpu *icpu = this_cpu_ptr(ipi_mux_pcpu);
	irq_hw_number_t hwirq;
	unsigned long ipis;
	unsigned int en;

	/*
	 * Reading enable mask does not need to be ordered as long as
	 * this function is called from interrupt handler because only
	 * the CPU itself can change it's own enable mask.
	 */
	en = atomic_read(&icpu->enable);

	/*
	 * Clear the IPIs we are about to handle. This pairs with the
	 * atomic_fetch_or_release() in ipi_mux_send_mask().
	 */
	ipis = atomic_fetch_andnot(en, &icpu->bits) & en;

	for_each_set_bit(hwirq, &ipis, BITS_PER_TYPE(int))
		generic_handle_domain_irq(ipi_mux_domain, hwirq);
}

/**
 * ipi_mux_create - Create virtual IPIs multiplexed on top of a single
 * parent IPI.
 * @nr_ipi:		number of virtual IPIs to create. This should
 *			be <= BITS_PER_TYPE(int)
 * @mux_send:		callback to trigger parent IPI for a particular CPU
 *
 * Returns first virq of the newly created virtual IPIs upon success
 * or <=0 upon failure
 */
int ipi_mux_create(unsigned int nr_ipi, void (*mux_send)(unsigned int cpu))
{
	struct fwnode_handle *fwnode;
	struct irq_domain *domain;
	int rc;

	if (ipi_mux_domain)
		return -EEXIST;

	if (BITS_PER_TYPE(int) < nr_ipi || !mux_send)
		return -EINVAL;

	ipi_mux_pcpu = alloc_percpu(typeof(*ipi_mux_pcpu));
	if (!ipi_mux_pcpu)
		return -ENOMEM;

	fwnode = irq_domain_alloc_named_fwnode("IPI-Mux");
	if (!fwnode) {
		pr_err("unable to create IPI Mux fwnode\n");
		rc = -ENOMEM;
		goto fail_free_cpu;
	}

	domain = irq_domain_create_linear(fwnode, nr_ipi,
					  &ipi_mux_domain_ops, NULL);
	if (!domain) {
		pr_err("unable to add IPI Mux domain\n");
		rc = -ENOMEM;
		goto fail_free_fwnode;
	}

	domain->flags |= IRQ_DOMAIN_FLAG_IPI_SINGLE;
	irq_domain_update_bus_token(domain, DOMAIN_BUS_IPI);

	rc = irq_domain_alloc_irqs(domain, nr_ipi, NUMA_NO_NODE, NULL);
	if (rc <= 0) {
		pr_err("unable to alloc IRQs from IPI Mux domain\n");
		goto fail_free_domain;
	}

	ipi_mux_domain = domain;
	ipi_mux_send = mux_send;

	return rc;

fail_free_domain:
	irq_domain_remove(domain);
fail_free_fwnode:
	irq_domain_free_fwnode(fwnode);
fail_free_cpu:
	free_percpu(ipi_mux_pcpu);
	return rc;
}
