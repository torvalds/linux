
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/cpu.h>

static int get_first_sibling(unsigned int cpu)
{
	unsigned int ret;

	ret = cpumask_first(topology_sibling_cpumask(cpu));
	if (ret < nr_cpu_ids)
		return ret;
	return cpu;
}

/*
 * Take a map of online CPUs and the number of available interrupt vectors
 * and generate an output cpumask suitable for spreading MSI/MSI-X vectors
 * so that they are distributed as good as possible around the CPUs.  If
 * more vectors than CPUs are available we'll map one to each CPU,
 * otherwise we map one to the first sibling of each socket.
 *
 * If there are more vectors than CPUs we will still only have one bit
 * set per CPU, but interrupt code will keep on assigning the vectors from
 * the start of the bitmap until we run out of vectors.
 */
struct cpumask *irq_create_affinity_mask(unsigned int *nr_vecs)
{
	struct cpumask *affinity_mask;
	unsigned int max_vecs = *nr_vecs;

	if (max_vecs == 1)
		return NULL;

	affinity_mask = kzalloc(cpumask_size(), GFP_KERNEL);
	if (!affinity_mask) {
		*nr_vecs = 1;
		return NULL;
	}

	get_online_cpus();
	if (max_vecs >= num_online_cpus()) {
		cpumask_copy(affinity_mask, cpu_online_mask);
		*nr_vecs = num_online_cpus();
	} else {
		unsigned int vecs = 0, cpu;

		for_each_online_cpu(cpu) {
			if (cpu == get_first_sibling(cpu)) {
				cpumask_set_cpu(cpu, affinity_mask);
				vecs++;
			}

			if (--max_vecs == 0)
				break;
		}
		*nr_vecs = vecs;
	}
	put_online_cpus();

	return affinity_mask;
}
