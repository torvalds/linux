/*
 * Define default pcounter functions
 * Note that often used pcounters use dedicated functions to get a speed increase.
 * (see DEFINE_PCOUNTER/REF_PCOUNTER_MEMBER)
 */

#include <linux/module.h>
#include <linux/pcounter.h>
#include <linux/smp.h>
#include <linux/cpumask.h>

static void pcounter_dyn_add(struct pcounter *self, int inc)
{
	per_cpu_ptr(self->per_cpu_values, smp_processor_id())[0] += inc;
}

static int pcounter_dyn_getval(const struct pcounter *self, int cpu)
{
	return per_cpu_ptr(self->per_cpu_values, cpu)[0];
}

int pcounter_getval(const struct pcounter *self)
{
	int res = 0, cpu;

	for_each_possible_cpu(cpu)
		res += self->getval(self, cpu);

	return res;
}
EXPORT_SYMBOL_GPL(pcounter_getval);

int pcounter_alloc(struct pcounter *self)
{
	int rc = 0;
	if (self->add == NULL) {
		self->per_cpu_values = alloc_percpu(int);
		if (self->per_cpu_values != NULL) {
			self->add    = pcounter_dyn_add;
			self->getval = pcounter_dyn_getval;
		} else
			rc = 1;
	}
	return rc;
}
EXPORT_SYMBOL_GPL(pcounter_alloc);

void pcounter_free(struct pcounter *self)
{
	if (self->per_cpu_values != NULL) {
		free_percpu(self->per_cpu_values);
		self->per_cpu_values = NULL;
		self->getval = NULL;
		self->add = NULL;
	}
}
EXPORT_SYMBOL_GPL(pcounter_free);

