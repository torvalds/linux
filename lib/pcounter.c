/*
 * Define default pcounter functions
 * Note that often used pcounters use dedicated functions to get a speed increase.
 * (see DEFINE_PCOUNTER/REF_PCOUNTER_MEMBER)
 */

#include <linux/module.h>
#include <linux/pcounter.h>
#include <linux/smp.h>

void pcounter_def_add(struct pcounter *self, int inc)
{
	per_cpu_ptr(self->per_cpu_values, smp_processor_id())[0] += inc;
}

EXPORT_SYMBOL_GPL(pcounter_def_add);

int pcounter_def_getval(const struct pcounter *self)
{
	int res = 0, cpu;
	for_each_possible_cpu(cpu)
		res += per_cpu_ptr(self->per_cpu_values, cpu)[0];
	return res;
}

EXPORT_SYMBOL_GPL(pcounter_def_getval);
