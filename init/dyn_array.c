#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/kallsyms.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/irq.h>

void __init pre_alloc_dyn_array(void)
{
#ifdef CONFIG_HAVE_DYN_ARRAY
	unsigned long total_size = 0, size, phys;
	unsigned long max_align = 1;
	struct dyn_array **daa;
	char *ptr;

	/* get the total size at first */
	for (daa = __dyn_array_start ; daa < __dyn_array_end; daa++) {
		struct dyn_array *da = *daa;

		printk(KERN_DEBUG "dyn_array %pF size:%#lx nr:%d align:%#lx\n",
			da->name, da->size, *da->nr, da->align);
		size = da->size * (*da->nr);
		total_size += roundup(size, da->align);
		if (da->align > max_align)
			max_align = da->align;
	}
	if (total_size)
		printk(KERN_DEBUG "dyn_array total_size: %#lx\n",
			 total_size);
	else
		return;

	/* allocate them all together */
	max_align = max_t(unsigned long, max_align, PAGE_SIZE);
	ptr = __alloc_bootmem(total_size, max_align, 0);
	phys = virt_to_phys(ptr);

	for (daa = __dyn_array_start ; daa < __dyn_array_end; daa++) {
		struct dyn_array *da = *daa;

		size = da->size * (*da->nr);
		phys = roundup(phys, da->align);
		printk(KERN_DEBUG "dyn_array %pF ==> [%#lx - %#lx]\n",
			da->name, phys, phys + size);
		*da->name = phys_to_virt(phys);

		phys += size;

		if (da->init_work)
			da->init_work(da);
	}
#else
#ifdef CONFIG_GENERIC_HARDIRQS
	unsigned int i;

	for (i = 0; i < NR_IRQS; i++)
		irq_desc[i].irq = i;
#endif
#endif
}

unsigned long __init per_cpu_dyn_array_size(unsigned long *align)
{
	unsigned long total_size = 0;
#ifdef CONFIG_HAVE_DYN_ARRAY
	unsigned long size;
	struct dyn_array **daa;
	unsigned max_align = 1;

	for (daa = __per_cpu_dyn_array_start ; daa < __per_cpu_dyn_array_end; daa++) {
		struct dyn_array *da = *daa;

		printk(KERN_DEBUG "per_cpu_dyn_array %pF size:%#lx nr:%d align:%#lx\n",
			da->name, da->size, *da->nr, da->align);
		size = da->size * (*da->nr);
		total_size += roundup(size, da->align);
		if (da->align > max_align)
			max_align = da->align;
	}
	if (total_size) {
		printk(KERN_DEBUG "per_cpu_dyn_array total_size: %#lx\n",
			 total_size);
		*align = max_align;
	}
#endif
	return total_size;
}

#ifdef CONFIG_SMP
void __init per_cpu_alloc_dyn_array(int cpu, char *ptr)
{
#ifdef CONFIG_HAVE_DYN_ARRAY
	unsigned long size, phys;
	struct dyn_array **daa;
	unsigned long addr;
	void **array;

	phys = virt_to_phys(ptr);
	for (daa = __per_cpu_dyn_array_start ; daa < __per_cpu_dyn_array_end; daa++) {
		struct dyn_array *da = *daa;

		size = da->size * (*da->nr);
		phys = roundup(phys, da->align);
		printk(KERN_DEBUG "per_cpu_dyn_array %pF ==> [%#lx - %#lx]\n",
			da->name, phys, phys + size);

		addr = (unsigned long)da->name;
		addr += per_cpu_offset(cpu);
		array = (void **)addr;
		*array = phys_to_virt(phys);
		*da->name = *array; /* so init_work could use it directly */

		phys += size;

		if (da->init_work)
			da->init_work(da);
	}
#endif
}
#endif
