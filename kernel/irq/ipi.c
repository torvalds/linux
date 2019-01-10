// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015 Imagination Technologies Ltd
 * Author: Qais Yousef <qais.yousef@imgtec.com>
 *
 * This file contains driver APIs to the IPI subsystem.
 */

#define pr_fmt(fmt) "genirq/ipi: " fmt

#include <linux/irqdomain.h>
#include <linux/irq.h>

/**
 * irq_reserve_ipi() - Setup an IPI to destination cpumask
 * @domain:	IPI domain
 * @dest:	cpumask of cpus which can receive the IPI
 *
 * Allocate a virq that can be used to send IPI to any CPU in dest mask.
 *
 * On success it'll return linux irq number and error code on failure
 */
int irq_reserve_ipi(struct irq_domain *domain,
			     const struct cpumask *dest)
{
	unsigned int nr_irqs, offset;
	struct irq_data *data;
	int virq, i;

	if (!domain ||!irq_domain_is_ipi(domain)) {
		pr_warn("Reservation on a non IPI domain\n");
		return -EINVAL;
	}

	if (!cpumask_subset(dest, cpu_possible_mask)) {
		pr_warn("Reservation is not in possible_cpu_mask\n");
		return -EINVAL;
	}

	nr_irqs = cpumask_weight(dest);
	if (!nr_irqs) {
		pr_warn("Reservation for empty destination mask\n");
		return -EINVAL;
	}

	if (irq_domain_is_ipi_single(domain)) {
		/*
		 * If the underlying implementation uses a single HW irq on
		 * all cpus then we only need a single Linux irq number for
		 * it. We have no restrictions vs. the destination mask. The
		 * underlying implementation can deal with holes nicely.
		 */
		nr_irqs = 1;
		offset = 0;
	} else {
		unsigned int next;

		/*
		 * The IPI requires a separate HW irq on each CPU. We require
		 * that the destination mask is consecutive. If an
		 * implementation needs to support holes, it can reserve
		 * several IPI ranges.
		 */
		offset = cpumask_first(dest);
		/*
		 * Find a hole and if found look for another set bit after the
		 * hole. For now we don't support this scenario.
		 */
		next = cpumask_next_zero(offset, dest);
		if (next < nr_cpu_ids)
			next = cpumask_next(next, dest);
		if (next < nr_cpu_ids) {
			pr_warn("Destination mask has holes\n");
			return -EINVAL;
		}
	}

	virq = irq_domain_alloc_descs(-1, nr_irqs, 0, NUMA_NO_NODE, NULL);
	if (virq <= 0) {
		pr_warn("Can't reserve IPI, failed to alloc descs\n");
		return -ENOMEM;
	}

	virq = __irq_domain_alloc_irqs(domain, virq, nr_irqs, NUMA_NO_NODE,
				       (void *) dest, true, NULL);

	if (virq <= 0) {
		pr_warn("Can't reserve IPI, failed to alloc hw irqs\n");
		goto free_descs;
	}

	for (i = 0; i < nr_irqs; i++) {
		data = irq_get_irq_data(virq + i);
		cpumask_copy(data->common->affinity, dest);
		data->common->ipi_offset = offset;
		irq_set_status_flags(virq + i, IRQ_NO_BALANCING);
	}
	return virq;

free_descs:
	irq_free_descs(virq, nr_irqs);
	return -EBUSY;
}

/**
 * irq_destroy_ipi() - unreserve an IPI that was previously allocated
 * @irq:	linux irq number to be destroyed
 * @dest:	cpumask of cpus which should have the IPI removed
 *
 * The IPIs allocated with irq_reserve_ipi() are retuerned to the system
 * destroying all virqs associated with them.
 *
 * Return 0 on success or error code on failure.
 */
int irq_destroy_ipi(unsigned int irq, const struct cpumask *dest)
{
	struct irq_data *data = irq_get_irq_data(irq);
	struct cpumask *ipimask = data ? irq_data_get_affinity_mask(data) : NULL;
	struct irq_domain *domain;
	unsigned int nr_irqs;

	if (!irq || !data || !ipimask)
		return -EINVAL;

	domain = data->domain;
	if (WARN_ON(domain == NULL))
		return -EINVAL;

	if (!irq_domain_is_ipi(domain)) {
		pr_warn("Trying to destroy a non IPI domain!\n");
		return -EINVAL;
	}

	if (WARN_ON(!cpumask_subset(dest, ipimask)))
		/*
		 * Must be destroying a subset of CPUs to which this IPI
		 * was set up to target
		 */
		return -EINVAL;

	if (irq_domain_is_ipi_per_cpu(domain)) {
		irq = irq + cpumask_first(dest) - data->common->ipi_offset;
		nr_irqs = cpumask_weight(dest);
	} else {
		nr_irqs = 1;
	}

	irq_domain_free_irqs(irq, nr_irqs);
	return 0;
}

/**
 * ipi_get_hwirq - Get the hwirq associated with an IPI to a cpu
 * @irq:	linux irq number
 * @cpu:	the target cpu
 *
 * When dealing with coprocessors IPI, we need to inform the coprocessor of
 * the hwirq it needs to use to receive and send IPIs.
 *
 * Returns hwirq value on success and INVALID_HWIRQ on failure.
 */
irq_hw_number_t ipi_get_hwirq(unsigned int irq, unsigned int cpu)
{
	struct irq_data *data = irq_get_irq_data(irq);
	struct cpumask *ipimask = data ? irq_data_get_affinity_mask(data) : NULL;

	if (!data || !ipimask || cpu >= nr_cpu_ids)
		return INVALID_HWIRQ;

	if (!cpumask_test_cpu(cpu, ipimask))
		return INVALID_HWIRQ;

	/*
	 * Get the real hardware irq number if the underlying implementation
	 * uses a separate irq per cpu. If the underlying implementation uses
	 * a single hardware irq for all cpus then the IPI send mechanism
	 * needs to take care of the cpu destinations.
	 */
	if (irq_domain_is_ipi_per_cpu(data->domain))
		data = irq_get_irq_data(irq + cpu - data->common->ipi_offset);

	return data ? irqd_to_hwirq(data) : INVALID_HWIRQ;
}
EXPORT_SYMBOL_GPL(ipi_get_hwirq);

static int ipi_send_verify(struct irq_chip *chip, struct irq_data *data,
			   const struct cpumask *dest, unsigned int cpu)
{
	struct cpumask *ipimask = irq_data_get_affinity_mask(data);

	if (!chip || !ipimask)
		return -EINVAL;

	if (!chip->ipi_send_single && !chip->ipi_send_mask)
		return -EINVAL;

	if (cpu >= nr_cpu_ids)
		return -EINVAL;

	if (dest) {
		if (!cpumask_subset(dest, ipimask))
			return -EINVAL;
	} else {
		if (!cpumask_test_cpu(cpu, ipimask))
			return -EINVAL;
	}
	return 0;
}

/**
 * __ipi_send_single - send an IPI to a target Linux SMP CPU
 * @desc:	pointer to irq_desc of the IRQ
 * @cpu:	destination CPU, must in the destination mask passed to
 *		irq_reserve_ipi()
 *
 * This function is for architecture or core code to speed up IPI sending. Not
 * usable from driver code.
 *
 * Returns zero on success and negative error number on failure.
 */
int __ipi_send_single(struct irq_desc *desc, unsigned int cpu)
{
	struct irq_data *data = irq_desc_get_irq_data(desc);
	struct irq_chip *chip = irq_data_get_irq_chip(data);

#ifdef DEBUG
	/*
	 * Minimise the overhead by omitting the checks for Linux SMP IPIs.
	 * Since the callers should be arch or core code which is generally
	 * trusted, only check for errors when debugging.
	 */
	if (WARN_ON_ONCE(ipi_send_verify(chip, data, NULL, cpu)))
		return -EINVAL;
#endif
	if (!chip->ipi_send_single) {
		chip->ipi_send_mask(data, cpumask_of(cpu));
		return 0;
	}

	/* FIXME: Store this information in irqdata flags */
	if (irq_domain_is_ipi_per_cpu(data->domain) &&
	    cpu != data->common->ipi_offset) {
		/* use the correct data for that cpu */
		unsigned irq = data->irq + cpu - data->common->ipi_offset;

		data = irq_get_irq_data(irq);
	}
	chip->ipi_send_single(data, cpu);
	return 0;
}

/**
 * ipi_send_mask - send an IPI to target Linux SMP CPU(s)
 * @desc:	pointer to irq_desc of the IRQ
 * @dest:	dest CPU(s), must be a subset of the mask passed to
 *		irq_reserve_ipi()
 *
 * This function is for architecture or core code to speed up IPI sending. Not
 * usable from driver code.
 *
 * Returns zero on success and negative error number on failure.
 */
int __ipi_send_mask(struct irq_desc *desc, const struct cpumask *dest)
{
	struct irq_data *data = irq_desc_get_irq_data(desc);
	struct irq_chip *chip = irq_data_get_irq_chip(data);
	unsigned int cpu;

#ifdef DEBUG
	/*
	 * Minimise the overhead by omitting the checks for Linux SMP IPIs.
	 * Since the callers should be arch or core code which is generally
	 * trusted, only check for errors when debugging.
	 */
	if (WARN_ON_ONCE(ipi_send_verify(chip, data, dest, 0)))
		return -EINVAL;
#endif
	if (chip->ipi_send_mask) {
		chip->ipi_send_mask(data, dest);
		return 0;
	}

	if (irq_domain_is_ipi_per_cpu(data->domain)) {
		unsigned int base = data->irq;

		for_each_cpu(cpu, dest) {
			unsigned irq = base + cpu - data->common->ipi_offset;

			data = irq_get_irq_data(irq);
			chip->ipi_send_single(data, cpu);
		}
	} else {
		for_each_cpu(cpu, dest)
			chip->ipi_send_single(data, cpu);
	}
	return 0;
}

/**
 * ipi_send_single - Send an IPI to a single CPU
 * @virq:	linux irq number from irq_reserve_ipi()
 * @cpu:	destination CPU, must in the destination mask passed to
 *		irq_reserve_ipi()
 *
 * Returns zero on success and negative error number on failure.
 */
int ipi_send_single(unsigned int virq, unsigned int cpu)
{
	struct irq_desc *desc = irq_to_desc(virq);
	struct irq_data *data = desc ? irq_desc_get_irq_data(desc) : NULL;
	struct irq_chip *chip = data ? irq_data_get_irq_chip(data) : NULL;

	if (WARN_ON_ONCE(ipi_send_verify(chip, data, NULL, cpu)))
		return -EINVAL;

	return __ipi_send_single(desc, cpu);
}
EXPORT_SYMBOL_GPL(ipi_send_single);

/**
 * ipi_send_mask - Send an IPI to target CPU(s)
 * @virq:	linux irq number from irq_reserve_ipi()
 * @dest:	dest CPU(s), must be a subset of the mask passed to
 *		irq_reserve_ipi()
 *
 * Returns zero on success and negative error number on failure.
 */
int ipi_send_mask(unsigned int virq, const struct cpumask *dest)
{
	struct irq_desc *desc = irq_to_desc(virq);
	struct irq_data *data = desc ? irq_desc_get_irq_data(desc) : NULL;
	struct irq_chip *chip = data ? irq_data_get_irq_chip(data) : NULL;

	if (WARN_ON_ONCE(ipi_send_verify(chip, data, dest, 0)))
		return -EINVAL;

	return __ipi_send_mask(desc, dest);
}
EXPORT_SYMBOL_GPL(ipi_send_mask);
