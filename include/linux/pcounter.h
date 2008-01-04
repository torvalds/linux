#ifndef __LINUX_PCOUNTER_H
#define __LINUX_PCOUNTER_H
/*
 * Using a dynamic percpu 'int' variable has a cost :
 * 1) Extra dereference
 * Current per_cpu_ptr() implementation uses an array per 'percpu variable'.
 * 2) memory cost of NR_CPUS*(32+sizeof(void *)) instead of num_possible_cpus()*4
 *
 * This pcounter implementation is an abstraction to be able to use
 * either a static or a dynamic per cpu variable.
 * One dynamic per cpu variable gets a fast & cheap implementation, we can
 * change pcounter implementation too.
 */
struct pcounter {
#ifdef CONFIG_SMP
	void		(*add)(struct pcounter *self, int inc);
	int		(*getval)(const struct pcounter *self, int cpu);
	int		*per_cpu_values;
#else
	int		val;
#endif
};

#ifdef CONFIG_SMP
#include <linux/percpu.h>

#define DEFINE_PCOUNTER(NAME)						\
static DEFINE_PER_CPU(int, NAME##_pcounter_values);			\
static void NAME##_pcounter_add(struct pcounter *self, int val)		\
{									\
       __get_cpu_var(NAME##_pcounter_values) += val;			\
}									\
static int NAME##_pcounter_getval(const struct pcounter *self, int cpu)	\
{									\
	return per_cpu(NAME##_pcounter_values, cpu);			\
}									\

#define PCOUNTER_MEMBER_INITIALIZER(NAME, MEMBER)		\
	MEMBER = {						\
		.add	= NAME##_pcounter_add,			\
		.getval = NAME##_pcounter_getval,		\
	}


static inline void pcounter_add(struct pcounter *self, int inc)
{
	self->add(self, inc);
}

extern int pcounter_getval(const struct pcounter *self);
extern int pcounter_alloc(struct pcounter *self);
extern void pcounter_free(struct pcounter *self);


#else /* CONFIG_SMP */

static inline void pcounter_add(struct pcounter *self, int inc)
{
	self->val += inc;
}

static inline int pcounter_getval(const struct pcounter *self)
{
	return self->val;
}

#define DEFINE_PCOUNTER(NAME)
#define PCOUNTER_MEMBER_INITIALIZER(NAME, MEMBER)
#define pcounter_alloc(self) 0
#define pcounter_free(self)

#endif /* CONFIG_SMP */

#endif /* __LINUX_PCOUNTER_H */
